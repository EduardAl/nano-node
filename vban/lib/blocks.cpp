#include <vban/crypto_lib/random_pool.hpp>
#include <vban/lib/blocks.hpp>
#include <vban/lib/memory.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/lib/threading.hpp>

#include <crypto/cryptopp/words.h>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <bitset>

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, vban::block const & second)
{
	static_assert (std::is_base_of<vban::block, T>::value, "Input parameter is not a block type");
	return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}

template <typename block>
std::shared_ptr<block> deserialize_block (vban::stream & stream_a)
{
	auto error (false);
	auto result = vban::make_shared<block> (error, stream_a);
	if (error)
	{
		result = nullptr;
	}

	return result;
}
}

void vban::block_memory_pool_purge ()
{
	vban::purge_shared_ptr_singleton_pool_memory<vban::open_block> ();
	vban::purge_shared_ptr_singleton_pool_memory<vban::state_block> ();
	vban::purge_shared_ptr_singleton_pool_memory<vban::send_block> ();
	vban::purge_shared_ptr_singleton_pool_memory<vban::change_block> ();
}

std::string vban::block::to_json () const
{
	std::string result;
	serialize_json (result);
	return result;
}

size_t vban::block::size (vban::block_type type_a)
{
	size_t result (0);
	switch (type_a)
	{
		case vban::block_type::invalid:
		case vban::block_type::not_a_block:
			debug_assert (false);
			break;
		case vban::block_type::send:
			result = vban::send_block::size;
			break;
		case vban::block_type::receive:
			result = vban::receive_block::size;
			break;
		case vban::block_type::change:
			result = vban::change_block::size;
			break;
		case vban::block_type::open:
			result = vban::open_block::size;
			break;
		case vban::block_type::state:
			result = vban::state_block::size;
			break;
	}
	return result;
}

vban::work_version vban::block::work_version () const
{
	return vban::work_version::work_1;
}

uint64_t vban::block::difficulty () const
{
	return vban::work_difficulty (this->work_version (), this->root (), this->block_work ());
}

vban::block_hash vban::block::generate_hash () const
{
	vban::block_hash result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	debug_assert (status == 0);
	hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	debug_assert (status == 0);
	return result;
}

void vban::block::refresh ()
{
	if (!cached_hash.is_zero ())
	{
		cached_hash = generate_hash ();
	}
}

vban::block_hash const & vban::block::hash () const
{
	if (!cached_hash.is_zero ())
	{
		// Once a block is created, it should not be modified (unless using refresh ())
		// This would invalidate the cache; check it hasn't changed.
		debug_assert (cached_hash == generate_hash ());
	}
	else
	{
		cached_hash = generate_hash ();
	}

	return cached_hash;
}

vban::block_hash vban::block::full_hash () const
{
	vban::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ()));
	auto signature (block_signature ());
	blake2b_update (&state, signature.bytes.data (), sizeof (signature));
	auto work (block_work ());
	blake2b_update (&state, &work, sizeof (work));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

vban::block_sideband const & vban::block::sideband () const
{
	debug_assert (sideband_m.is_initialized ());
	return *sideband_m;
}

void vban::block::sideband_set (vban::block_sideband const & sideband_a)
{
	sideband_m = sideband_a;
}

bool vban::block::has_sideband () const
{
	return sideband_m.is_initialized ();
}

vban::account const & vban::block::representative () const
{
	static vban::account rep{ 0 };
	return rep;
}

vban::block_hash const & vban::block::source () const
{
	static vban::block_hash source{ 0 };
	return source;
}

vban::account const & vban::block::destination () const
{
	static vban::account destination{ 0 };
	return destination;
}

vban::link const & vban::block::link () const
{
	static vban::link link{ 0 };
	return link;
}

vban::account const & vban::block::account () const
{
	static vban::account account{ 0 };
	return account;
}

vban::qualified_root vban::block::qualified_root () const
{
	return vban::qualified_root (root (), previous ());
}

vban::amount const & vban::block::balance () const
{
	static vban::amount amount{ 0 };
	return amount;
}

void vban::send_block::visit (vban::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void vban::send_block::visit (vban::mutable_block_visitor & visitor_a)
{
	visitor_a.send_block (*this);
}

void vban::send_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t vban::send_block::block_work () const
{
	return work;
}

void vban::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

vban::send_hashables::send_hashables (vban::block_hash const & previous_a, vban::account const & destination_a, vban::amount const & balance_a) :
	previous (previous_a),
	destination (destination_a),
	balance (balance_a)
{
}

vban::send_hashables::send_hashables (bool & error_a, vban::stream & stream_a)
{
	try
	{
		vban::read (stream_a, previous.bytes);
		vban::read (stream_a, destination.bytes);
		vban::read (stream_a, balance.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

vban::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = destination.decode_account (destination_l);
			if (!error_a)
			{
				error_a = balance.decode_hex (balance_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void vban::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	debug_assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	debug_assert (status == 0);
}

void vban::send_block::serialize (vban::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool vban::send_block::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.destination.bytes);
		read (stream_a, hashables.balance.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::exception const &)
	{
		error = true;
	}

	return error;
}

void vban::send_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void vban::send_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "send");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	tree.put ("destination", hashables.destination.to_account ());
	std::string balance;
	hashables.balance.encode_hex (balance);
	tree.put ("balance", balance);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", vban::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool vban::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "send");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.destination.decode_account (destination_l);
			if (!error)
			{
				error = hashables.balance.decode_hex (balance_l);
				if (!error)
				{
					error = vban::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

vban::send_block::send_block (vban::block_hash const & previous_a, vban::account const & destination_a, vban::amount const & balance_a, vban::raw_key const & prv_a, vban::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, destination_a, balance_a),
	signature (vban::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
}

vban::send_block::send_block (bool & error_a, vban::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			vban::read (stream_a, signature.bytes);
			vban::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

vban::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = vban::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

bool vban::send_block::operator== (vban::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool vban::send_block::valid_predecessor (vban::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case vban::block_type::send:
		case vban::block_type::receive:
		case vban::block_type::open:
		case vban::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

vban::block_type vban::send_block::type () const
{
	return vban::block_type::send;
}

bool vban::send_block::operator== (vban::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

vban::block_hash const & vban::send_block::previous () const
{
	return hashables.previous;
}

vban::account const & vban::send_block::destination () const
{
	return hashables.destination;
}

vban::root const & vban::send_block::root () const
{
	return hashables.previous;
}

vban::amount const & vban::send_block::balance () const
{
	return hashables.balance;
}

vban::signature const & vban::send_block::block_signature () const
{
	return signature;
}

void vban::send_block::signature_set (vban::signature const & signature_a)
{
	signature = signature_a;
}

vban::open_hashables::open_hashables (vban::block_hash const & source_a, vban::account const & representative_a, vban::account const & account_a) :
	source (source_a),
	representative (representative_a),
	account (account_a)
{
}

vban::open_hashables::open_hashables (bool & error_a, vban::stream & stream_a)
{
	try
	{
		vban::read (stream_a, source.bytes);
		vban::read (stream_a, representative.bytes);
		vban::read (stream_a, account.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

vban::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		error_a = source.decode_hex (source_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
			if (!error_a)
			{
				error_a = account.decode_account (account_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void vban::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

vban::open_block::open_block (vban::block_hash const & source_a, vban::account const & representative_a, vban::account const & account_a, vban::raw_key const & prv_a, vban::public_key const & pub_a, uint64_t work_a) :
	hashables (source_a, representative_a, account_a),
	signature (vban::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
	debug_assert (!representative_a.is_zero ());
}

vban::open_block::open_block (vban::block_hash const & source_a, vban::account const & representative_a, vban::account const & account_a, std::nullptr_t) :
	hashables (source_a, representative_a, account_a),
	work (0)
{
	signature.clear ();
}

vban::open_block::open_block (bool & error_a, vban::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			vban::read (stream_a, signature);
			vban::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

vban::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = vban::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void vban::open_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t vban::open_block::block_work () const
{
	return work;
}

void vban::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

vban::block_hash const & vban::open_block::previous () const
{
	static vban::block_hash result{ 0 };
	return result;
}

vban::account const & vban::open_block::account () const
{
	return hashables.account;
}

void vban::open_block::serialize (vban::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

bool vban::open_block::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.source);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.account);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void vban::open_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void vban::open_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("account", hashables.account.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", vban::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool vban::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "open");
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.source.decode_hex (source_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = hashables.account.decode_hex (account_l);
				if (!error)
				{
					error = vban::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void vban::open_block::visit (vban::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

void vban::open_block::visit (vban::mutable_block_visitor & visitor_a)
{
	visitor_a.open_block (*this);
}

vban::block_type vban::open_block::type () const
{
	return vban::block_type::open;
}

bool vban::open_block::operator== (vban::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool vban::open_block::operator== (vban::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

bool vban::open_block::valid_predecessor (vban::block const & block_a) const
{
	return false;
}

vban::block_hash const & vban::open_block::source () const
{
	return hashables.source;
}

vban::root const & vban::open_block::root () const
{
	return hashables.account;
}

vban::account const & vban::open_block::representative () const
{
	return hashables.representative;
}

vban::signature const & vban::open_block::block_signature () const
{
	return signature;
}

void vban::open_block::signature_set (vban::signature const & signature_a)
{
	signature = signature_a;
}

vban::change_hashables::change_hashables (vban::block_hash const & previous_a, vban::account const & representative_a) :
	previous (previous_a),
	representative (representative_a)
{
}

vban::change_hashables::change_hashables (bool & error_a, vban::stream & stream_a)
{
	try
	{
		vban::read (stream_a, previous);
		vban::read (stream_a, representative);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

vban::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void vban::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

vban::change_block::change_block (vban::block_hash const & previous_a, vban::account const & representative_a, vban::raw_key const & prv_a, vban::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, representative_a),
	signature (vban::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
}

vban::change_block::change_block (bool & error_a, vban::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			vban::read (stream_a, signature);
			vban::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

vban::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = vban::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void vban::change_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t vban::change_block::block_work () const
{
	return work;
}

void vban::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

vban::block_hash const & vban::change_block::previous () const
{
	return hashables.previous;
}

void vban::change_block::serialize (vban::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

bool vban::change_block::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void vban::change_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void vban::change_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("work", vban::to_string_hex (work));
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
}

bool vban::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "change");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = vban::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void vban::change_block::visit (vban::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

void vban::change_block::visit (vban::mutable_block_visitor & visitor_a)
{
	visitor_a.change_block (*this);
}

vban::block_type vban::change_block::type () const
{
	return vban::block_type::change;
}

bool vban::change_block::operator== (vban::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool vban::change_block::operator== (vban::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

bool vban::change_block::valid_predecessor (vban::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case vban::block_type::send:
		case vban::block_type::receive:
		case vban::block_type::open:
		case vban::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

vban::root const & vban::change_block::root () const
{
	return hashables.previous;
}

vban::account const & vban::change_block::representative () const
{
	return hashables.representative;
}

vban::signature const & vban::change_block::block_signature () const
{
	return signature;
}

void vban::change_block::signature_set (vban::signature const & signature_a)
{
	signature = signature_a;
}

vban::state_hashables::state_hashables (vban::account const & account_a, vban::block_hash const & previous_a, vban::account const & representative_a, vban::amount const & balance_a, vban::link const & link_a) :
	account (account_a),
	previous (previous_a),
	representative (representative_a),
	balance (balance_a),
	link (link_a)
{
}

vban::state_hashables::state_hashables (bool & error_a, vban::stream & stream_a)
{
	try
	{
		vban::read (stream_a, account);
		vban::read (stream_a, previous);
		vban::read (stream_a, representative);
		vban::read (stream_a, balance);
		vban::read (stream_a, link);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

vban::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		error_a = account.decode_account (account_l);
		if (!error_a)
		{
			error_a = previous.decode_hex (previous_l);
			if (!error_a)
			{
				error_a = representative.decode_account (representative_l);
				if (!error_a)
				{
					error_a = balance.decode_dec (balance_l);
					if (!error_a)
					{
						error_a = link.decode_account (link_l) && link.decode_hex (link_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void vban::state_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

vban::state_block::state_block (vban::account const & account_a, vban::block_hash const & previous_a, vban::account const & representative_a, vban::amount const & balance_a, vban::link const & link_a, vban::raw_key const & prv_a, vban::public_key const & pub_a, uint64_t work_a) :
	hashables (account_a, previous_a, representative_a, balance_a, link_a),
	signature (vban::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
}

vban::state_block::state_block (bool & error_a, vban::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			vban::read (stream_a, signature);
			vban::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

vban::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto type_l (tree_a.get<std::string> ("type"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = type_l != "state";
			if (!error_a)
			{
				error_a = vban::from_string_hex (work_l, work);
				if (!error_a)
				{
					error_a = signature.decode_hex (signature_l);
				}
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void vban::state_block::hash (blake2b_state & hash_a) const
{
	vban::uint256_union preamble (static_cast<uint64_t> (vban::block_type::state));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t vban::state_block::block_work () const
{
	return work;
}

void vban::state_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

vban::block_hash const & vban::state_block::previous () const
{
	return hashables.previous;
}

vban::account const & vban::state_block::account () const
{
	return hashables.account;
}

void vban::state_block::serialize (vban::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.link);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

bool vban::state_block::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.account);
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.balance);
		read (stream_a, hashables.link);
		read (stream_a, signature);
		read (stream_a, work);
		boost::endian::big_to_native_inplace (work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void vban::state_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void vban::state_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "state");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("link", hashables.link.to_string ());
	tree.put ("link_as_account", hashables.link.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", vban::to_string_hex (work));
}

bool vban::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "state");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.account.decode_account (account_l);
		if (!error)
		{
			error = hashables.previous.decode_hex (previous_l);
			if (!error)
			{
				error = hashables.representative.decode_account (representative_l);
				if (!error)
				{
					error = hashables.balance.decode_dec (balance_l);
					if (!error)
					{
						error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
						if (!error)
						{
							error = vban::from_string_hex (work_l, work);
							if (!error)
							{
								error = signature.decode_hex (signature_l);
							}
						}
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void vban::state_block::visit (vban::block_visitor & visitor_a) const
{
	visitor_a.state_block (*this);
}

void vban::state_block::visit (vban::mutable_block_visitor & visitor_a)
{
	visitor_a.state_block (*this);
}

vban::block_type vban::state_block::type () const
{
	return vban::block_type::state;
}

bool vban::state_block::operator== (vban::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool vban::state_block::operator== (vban::state_block const & other_a) const
{
	return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool vban::state_block::valid_predecessor (vban::block const & block_a) const
{
	return true;
}

vban::root const & vban::state_block::root () const
{
	if (!hashables.previous.is_zero ())
	{
		return hashables.previous;
	}
	else
	{
		return hashables.account;
	}
}

vban::link const & vban::state_block::link () const
{
	return hashables.link;
}

vban::account const & vban::state_block::representative () const
{
	return hashables.representative;
}

vban::amount const & vban::state_block::balance () const
{
	return hashables.balance;
}

vban::signature const & vban::state_block::block_signature () const
{
	return signature;
}

void vban::state_block::signature_set (vban::signature const & signature_a)
{
	signature = signature_a;
}

std::shared_ptr<vban::block> vban::deserialize_block_json (boost::property_tree::ptree const & tree_a, vban::block_uniquer * uniquer_a)
{
	std::shared_ptr<vban::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		bool error (false);
		std::unique_ptr<vban::block> obj;
		if (type == "receive")
		{
			obj = std::make_unique<vban::receive_block> (error, tree_a);
		}
		else if (type == "send")
		{
			obj = std::make_unique<vban::send_block> (error, tree_a);
		}
		else if (type == "open")
		{
			obj = std::make_unique<vban::open_block> (error, tree_a);
		}
		else if (type == "change")
		{
			obj = std::make_unique<vban::change_block> (error, tree_a);
		}
		else if (type == "state")
		{
			obj = std::make_unique<vban::state_block> (error, tree_a);
		}

		if (!error)
		{
			result = std::move (obj);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

std::shared_ptr<vban::block> vban::deserialize_block (vban::stream & stream_a)
{
	vban::block_type type;
	auto error (try_read (stream_a, type));
	std::shared_ptr<vban::block> result;
	if (!error)
	{
		result = vban::deserialize_block (stream_a, type);
	}
	return result;
}

std::shared_ptr<vban::block> vban::deserialize_block (vban::stream & stream_a, vban::block_type type_a, vban::block_uniquer * uniquer_a)
{
	std::shared_ptr<vban::block> result;
	switch (type_a)
	{
		case vban::block_type::receive:
		{
			result = ::deserialize_block<vban::receive_block> (stream_a);
			break;
		}
		case vban::block_type::send:
		{
			result = ::deserialize_block<vban::send_block> (stream_a);
			break;
		}
		case vban::block_type::open:
		{
			result = ::deserialize_block<vban::open_block> (stream_a);
			break;
		}
		case vban::block_type::change:
		{
			result = ::deserialize_block<vban::change_block> (stream_a);
			break;
		}
		case vban::block_type::state:
		{
			result = ::deserialize_block<vban::state_block> (stream_a);
			break;
		}
		default:
#ifndef VBAN_FUZZER_TEST
			debug_assert (false);
#endif
			break;
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

void vban::receive_block::visit (vban::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

void vban::receive_block::visit (vban::mutable_block_visitor & visitor_a)
{
	visitor_a.receive_block (*this);
}

bool vban::receive_block::operator== (vban::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

void vban::receive_block::serialize (vban::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool vban::receive_block::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.source.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void vban::receive_block::serialize_json (std::string & string_a, bool single_line) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree, !single_line);
	string_a = ostream.str ();
}

void vban::receive_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "receive");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	std::string source;
	hashables.source.encode_hex (source);
	tree.put ("source", source);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", vban::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool vban::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		debug_assert (tree_a.get<std::string> ("type") == "receive");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.source.decode_hex (source_l);
			if (!error)
			{
				error = vban::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

vban::receive_block::receive_block (vban::block_hash const & previous_a, vban::block_hash const & source_a, vban::raw_key const & prv_a, vban::public_key const & pub_a, uint64_t work_a) :
	hashables (previous_a, source_a),
	signature (vban::sign_message (prv_a, pub_a, hash ())),
	work (work_a)
{
}

vban::receive_block::receive_block (bool & error_a, vban::stream & stream_a) :
	hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			vban::read (stream_a, signature);
			vban::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

vban::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
	hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = vban::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void vban::receive_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t vban::receive_block::block_work () const
{
	return work;
}

void vban::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool vban::receive_block::operator== (vban::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool vban::receive_block::valid_predecessor (vban::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case vban::block_type::send:
		case vban::block_type::receive:
		case vban::block_type::open:
		case vban::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

vban::block_hash const & vban::receive_block::previous () const
{
	return hashables.previous;
}

vban::block_hash const & vban::receive_block::source () const
{
	return hashables.source;
}

vban::root const & vban::receive_block::root () const
{
	return hashables.previous;
}

vban::signature const & vban::receive_block::block_signature () const
{
	return signature;
}

void vban::receive_block::signature_set (vban::signature const & signature_a)
{
	signature = signature_a;
}

vban::block_type vban::receive_block::type () const
{
	return vban::block_type::receive;
}

vban::receive_hashables::receive_hashables (vban::block_hash const & previous_a, vban::block_hash const & source_a) :
	previous (previous_a),
	source (source_a)
{
}

vban::receive_hashables::receive_hashables (bool & error_a, vban::stream & stream_a)
{
	try
	{
		vban::read (stream_a, previous.bytes);
		vban::read (stream_a, source.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

vban::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = source.decode_hex (source_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void vban::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}

vban::block_details::block_details (vban::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a) :
	epoch (epoch_a), is_send (is_send_a), is_receive (is_receive_a), is_epoch (is_epoch_a)
{
}

bool vban::block_details::operator== (vban::block_details const & other_a) const
{
	return epoch == other_a.epoch && is_send == other_a.is_send && is_receive == other_a.is_receive && is_epoch == other_a.is_epoch;
}

uint8_t vban::block_details::packed () const
{
	std::bitset<8> result (static_cast<uint8_t> (epoch));
	result.set (7, is_send);
	result.set (6, is_receive);
	result.set (5, is_epoch);
	return static_cast<uint8_t> (result.to_ulong ());
}

void vban::block_details::unpack (uint8_t details_a)
{
	constexpr std::bitset<8> epoch_mask{ 0b00011111 };
	auto as_bitset = static_cast<std::bitset<8>> (details_a);
	is_send = as_bitset.test (7);
	is_receive = as_bitset.test (6);
	is_epoch = as_bitset.test (5);
	epoch = static_cast<vban::epoch> ((as_bitset & epoch_mask).to_ulong ());
}

void vban::block_details::serialize (vban::stream & stream_a) const
{
	vban::write (stream_a, packed ());
}

bool vban::block_details::deserialize (vban::stream & stream_a)
{
	bool result (false);
	try
	{
		uint8_t packed{ 0 };
		vban::read (stream_a, packed);
		unpack (packed);
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

std::string vban::state_subtype (vban::block_details const details_a)
{
	debug_assert (details_a.is_epoch + details_a.is_receive + details_a.is_send <= 1);
	if (details_a.is_send)
	{
		return "send";
	}
	else if (details_a.is_receive)
	{
		return "receive";
	}
	else if (details_a.is_epoch)
	{
		return "epoch";
	}
	else
	{
		return "change";
	}
}

vban::block_sideband::block_sideband (vban::account const & account_a, vban::block_hash const & successor_a, vban::amount const & balance_a, uint64_t const height_a, uint64_t const timestamp_a, vban::block_details const & details_a, vban::epoch const source_epoch_a) :
	successor (successor_a),
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (details_a),
	source_epoch (source_epoch_a)
{
}

vban::block_sideband::block_sideband (vban::account const & account_a, vban::block_hash const & successor_a, vban::amount const & balance_a, uint64_t const height_a, uint64_t const timestamp_a, vban::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, vban::epoch const source_epoch_a) :
	successor (successor_a),
	account (account_a),
	balance (balance_a),
	height (height_a),
	timestamp (timestamp_a),
	details (epoch_a, is_send, is_receive, is_epoch),
	source_epoch (source_epoch_a)
{
}

size_t vban::block_sideband::size (vban::block_type type_a)
{
	size_t result (0);
	result += sizeof (successor);
	if (type_a != vban::block_type::state && type_a != vban::block_type::open)
	{
		result += sizeof (account);
	}
	if (type_a != vban::block_type::open)
	{
		result += sizeof (height);
	}
	if (type_a == vban::block_type::receive || type_a == vban::block_type::change || type_a == vban::block_type::open)
	{
		result += sizeof (balance);
	}
	result += sizeof (timestamp);
	if (type_a == vban::block_type::state)
	{
		static_assert (sizeof (vban::epoch) == vban::block_details::size (), "block_details is larger than the epoch enum");
		result += vban::block_details::size () + sizeof (vban::epoch);
	}
	return result;
}

void vban::block_sideband::serialize (vban::stream & stream_a, vban::block_type type_a) const
{
	vban::write (stream_a, successor.bytes);
	if (type_a != vban::block_type::state && type_a != vban::block_type::open)
	{
		vban::write (stream_a, account.bytes);
	}
	if (type_a != vban::block_type::open)
	{
		vban::write (stream_a, boost::endian::native_to_big (height));
	}
	if (type_a == vban::block_type::receive || type_a == vban::block_type::change || type_a == vban::block_type::open)
	{
		vban::write (stream_a, balance.bytes);
	}
	vban::write (stream_a, boost::endian::native_to_big (timestamp));
	if (type_a == vban::block_type::state)
	{
		details.serialize (stream_a);
		vban::write (stream_a, static_cast<uint8_t> (source_epoch));
	}
}

bool vban::block_sideband::deserialize (vban::stream & stream_a, vban::block_type type_a)
{
	bool result (false);
	try
	{
		vban::read (stream_a, successor.bytes);
		if (type_a != vban::block_type::state && type_a != vban::block_type::open)
		{
			vban::read (stream_a, account.bytes);
		}
		if (type_a != vban::block_type::open)
		{
			vban::read (stream_a, height);
			boost::endian::big_to_native_inplace (height);
		}
		else
		{
			height = 1;
		}
		if (type_a == vban::block_type::receive || type_a == vban::block_type::change || type_a == vban::block_type::open)
		{
			vban::read (stream_a, balance.bytes);
		}
		vban::read (stream_a, timestamp);
		boost::endian::big_to_native_inplace (timestamp);
		if (type_a == vban::block_type::state)
		{
			result = details.deserialize (stream_a);
			uint8_t source_epoch_uint8_t{ 0 };
			vban::read (stream_a, source_epoch_uint8_t);
			source_epoch = static_cast<vban::epoch> (source_epoch_uint8_t);
		}
	}
	catch (std::runtime_error &)
	{
		result = true;
	}

	return result;
}

std::shared_ptr<vban::block> vban::block_uniquer::unique (std::shared_ptr<vban::block> const & block_a)
{
	auto result (block_a);
	if (result != nullptr)
	{
		vban::uint256_union key (block_a->full_hash ());
		vban::lock_guard<vban::mutex> lock (mutex);
		auto & existing (blocks[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = block_a;
		}
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > blocks.size ());
		for (auto i (0); i < cleanup_count && !blocks.empty (); ++i)
		{
			auto random_offset (vban::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (blocks.size () - 1)));
			auto existing (std::next (blocks.begin (), random_offset));
			if (existing == blocks.end ())
			{
				existing = blocks.begin ();
			}
			if (existing != blocks.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					blocks.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t vban::block_uniquer::size ()
{
	vban::lock_guard<vban::mutex> lock (mutex);
	return blocks.size ();
}

std::unique_ptr<vban::container_info_component> vban::collect_container_info (block_uniquer & block_uniquer, std::string const & name)
{
	auto count = block_uniquer.size ();
	auto sizeof_element = sizeof (block_uniquer::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "blocks", count, sizeof_element }));
	return composite;
}
