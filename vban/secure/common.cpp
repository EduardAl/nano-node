#include <vban/crypto_lib/random_pool.hpp>
#include <vban/lib/config.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/secure/blockstore.hpp>
#include <vban/secure/common.hpp>

#include <crypto/cryptopp/words.h>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/variant/get.hpp>

#include <limits>
#include <queue>

#include <crypto/ed25519-donna/ed25519.h>

size_t constexpr vban::send_block::size;
size_t constexpr vban::receive_block::size;
size_t constexpr vban::open_block::size;
size_t constexpr vban::change_block::size;
size_t constexpr vban::state_block::size;

vban::vban_networks vban::network_constants::active_network = vban::vban_networks::ACTIVE_NETWORK;

namespace
{
char const * dev_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * dev_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "259A43ABDB779E97452E188BA3EB951B41C961D3318CA6B925380F4D99F0577A"; // vban_1betagoxpxwykx4kw86dnhosc8t3s7ix8eeentwkcg1hbpez1outjrcyg4n1
char const * live_public_key_data = "2F0C7F5856CFCDC49559B66FD904028160B58BB19D51694882993D3F1693A0D9"; // xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3
std::string const test_public_key_data = vban::get_env_or_default ("VBAN_TEST_GENESIS_PUB", "45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED"); // vban_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j
char const * dev_genesis_data = R"%%%({
	"type": "open",
	"source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
	"representative": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"account": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"work": "7b42a00ee91d5810",
	"signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
	})%%%";

char const * beta_genesis_data = R"%%%({
	"type": "open",
	"source": "259A43ABDB779E97452E188BA3EB951B41C961D3318CA6B925380F4D99F0577A",
	"representative": "vban_1betagoxpxwykx4kw86dnhosc8t3s7ix8eeentwkcg1hbpez1outjrcyg4n1",
	"account": "vban_1betagoxpxwykx4kw86dnhosc8t3s7ix8eeentwkcg1hbpez1outjrcyg4n1",
	"work": "79d4e27dc873c6f2",
	"signature": "4BD7F96F9ED2721BCEE5EAED400EA50AD00524C629AE55E9AFF11220D2C1B00C3D4B3BB770BF67D4F8658023B677F91110193B6C101C2666931F57046A6DB806"
	})%%%";

char const * live_genesis_data = R"%%%({
    	"type": "open",
    	"source": "2F0C7F5856CFCDC49559B66FD904028160B58BB19D51694882993D3F1693A0D9",
    	"representative": "vban_1drehxe7fmyfrkcomfmhu64171d1pp7u59cjf76a78bx9wdb9a8ss7wxwcni",
    	"account": "vban_1drehxe7fmyfrkcomfmhu64171d1pp7u59cjf76a78bx9wdb9a8ss7wxwcni",
    	"work": "ea3b81caea0d1935",
    	"signature": "01E64592D88FB1EFCF4050D66EDAA2D95BE207EB25BAA5AD469441F5503D79FFC14D40F21AB0D5FF59C2B9E264AD60E8D271D89734AC61C79209FADB2D7AFC0F"
	})%%%";

std::string const test_genesis_data = vban::get_env_or_default ("VBAN_TEST_GENESIS_BLOCK", R"%%%({
	"type": "open",
	"source": "45C6FF9D1706D61F0821327752671BDA9F9ED2DA40326B01935AB566FB9E08ED",
	"representative": "vban_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"account": "vban_1jg8zygjg3pp5w644emqcbmjqpnzmubfni3kfe1s8pooeuxsw49fdq1mco9j",
	"work": "bc1ef279c1a34eb1",
	"signature": "15049467CAEE3EC768639E8E35792399B6078DA763DA4EBA8ECAD33B0EDC4AF2E7403893A5A602EB89B978DABEF1D6606BB00F3C0EE11449232B143B6E07170E"
	})%%%");

std::shared_ptr<vban::block> parse_block_from_genesis_data (std::string const & genesis_data_a)
{
	boost::property_tree::ptree tree;
	std::stringstream istream (genesis_data_a);
	boost::property_tree::read_json (istream, tree);
	return vban::deserialize_block_json (tree);
}

char const * beta_canary_public_key_data = "868C6A9F79D4506E029B378262B91538C5CB26D7C346B63902FFEB365F1C1947"; // vban_33nefchqmo4ifr3bpfw4ecwjcg87semfhit8prwi7zzd8shjr8c9qdxeqmnx
char const * live_canary_public_key_data = "7CBAF192A3763DAEC9F9BAC1B2CDF665D8369F8400B4BC5AB4BA31C00BAA4404"; // vban_1z7ty8bc8xjxou6zmgp3pd8zesgr8thra17nqjfdbgjjr17tnj16fjntfqfn
std::string const test_canary_public_key_data = vban::get_env_or_default ("VBAN_TEST_CANARY_PUB", "3BAD2C554ACE05F5E528FBBCE79D51E552C55FA765CCFD89B289C4835DE5F04A"); // vban_1gxf7jcnomi7yqkkjyxwwygo5sckrohtgsgezp6u74g6ifgydw4cajwbk8bf
}

vban::network_params::network_params () :
	network_params (network_constants::active_network)
{
}

vban::network_params::network_params (vban::vban_networks network_a) :
	network (network_a), ledger (network), voting (network), node (network), portmapping (network), bootstrap (network)
{
	unsigned constexpr kdf_full_work = 64 * 1024;
	unsigned constexpr kdf_dev_work = 8;
	kdf_work = network.is_dev_network () ? kdf_dev_work : kdf_full_work;
	header_magic_number = network.is_dev_network () ? std::array<uint8_t, 2>{ { 'R', 'A' } } : network.is_beta_network () ? std::array<uint8_t, 2>{ { 'R', 'B' } } : network.is_live_network () ? std::array<uint8_t, 2>{ { 'R', 'C' } } : vban::test_magic_number ();
}

uint8_t vban::protocol_constants::protocol_version_min () const
{
	return protocol_version_min_m;
}

vban::ledger_constants::ledger_constants (vban::network_constants & network_constants) :
	ledger_constants (network_constants.network ())
{
}

vban::ledger_constants::ledger_constants (vban::vban_networks network_a) :
	zero_key ("0"),
	dev_genesis_key (dev_private_key_data),
	vban_dev_account (dev_public_key_data),
	vban_beta_account (beta_public_key_data),
	vban_live_account (live_public_key_data),
	vban_test_account (test_public_key_data),
	vban_dev_genesis (dev_genesis_data),
	vban_beta_genesis (beta_genesis_data),
	vban_live_genesis (live_genesis_data),
	vban_test_genesis (test_genesis_data),
	genesis_account (network_a == vban::vban_networks::vban_dev_network ? vban_dev_account : network_a == vban::vban_networks::vban_beta_network ? vban_beta_account : network_a == vban::vban_networks::vban_test_network ? vban_test_account : vban_live_account),
	genesis_block (network_a == vban::vban_networks::vban_dev_network ? vban_dev_genesis : network_a == vban::vban_networks::vban_beta_network ? vban_beta_genesis : network_a == vban::vban_networks::vban_test_network ? vban_test_genesis : vban_live_genesis),
	genesis_hash (parse_block_from_genesis_data (genesis_block)->hash ()),
	genesis_amount (vban::uint256_t ("50000000000000000000000000000000000000")),
	burn_account (0),
	vban_dev_final_votes_canary_account (dev_public_key_data),
	vban_beta_final_votes_canary_account (beta_canary_public_key_data),
	vban_live_final_votes_canary_account (live_canary_public_key_data),
	vban_test_final_votes_canary_account (test_canary_public_key_data),
	final_votes_canary_account (network_a == vban::vban_networks::vban_dev_network ? vban_dev_final_votes_canary_account : network_a == vban::vban_networks::vban_beta_network ? vban_beta_final_votes_canary_account : network_a == vban::vban_networks::vban_test_network ? vban_test_final_votes_canary_account : vban_live_final_votes_canary_account),
	vban_dev_final_votes_canary_height (1),
	vban_beta_final_votes_canary_height (1),
	vban_live_final_votes_canary_height (1),
	vban_test_final_votes_canary_height (1),
	final_votes_canary_height (network_a == vban::vban_networks::vban_dev_network ? vban_dev_final_votes_canary_height : network_a == vban::vban_networks::vban_beta_network ? vban_beta_final_votes_canary_height : network_a == vban::vban_networks::vban_test_network ? vban_test_final_votes_canary_height : vban_live_final_votes_canary_height)
{
	vban::link epoch_link_v1;
	const char * epoch_message_v1 ("epoch v1 block");
	strncpy ((char *)epoch_link_v1.bytes.data (), epoch_message_v1, epoch_link_v1.bytes.size ());
	epochs.add (vban::epoch::epoch_1, genesis_account, epoch_link_v1);

	vban::link epoch_link_v2;
	vban::account vban_live_epoch_v2_signer;
	auto error (vban_live_epoch_v2_signer.decode_account ("vban_3qb6o6i1tkzr6jwr5s7eehfxwg9x6eemitdinbpi7u8bjjwsgqfj4wzser3x"));
	debug_assert (!error);
	auto epoch_v2_signer (network_a == vban::vban_networks::vban_dev_network ? vban_dev_account : network_a == vban::vban_networks::vban_beta_network ? vban_beta_account : network_a == vban::vban_networks::vban_test_network ? vban_test_account : vban_live_epoch_v2_signer);
	const char * epoch_message_v2 ("epoch v2 block");
	strncpy ((char *)epoch_link_v2.bytes.data (), epoch_message_v2, epoch_link_v2.bytes.size ());
	epochs.add (vban::epoch::epoch_2, epoch_v2_signer, epoch_link_v2);
}

vban::random_constants::random_constants ()
{
	vban::random_pool::generate_block (not_an_account.bytes.data (), not_an_account.bytes.size ());
	vban::random_pool::generate_block (random_128.bytes.data (), random_128.bytes.size ());
}

vban::node_constants::node_constants (vban::network_constants & network_constants)
{
	period = network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (60);
	half_period = network_constants.is_dev_network () ? std::chrono::milliseconds (500) : std::chrono::milliseconds (30 * 1000);
	idle_timeout = network_constants.is_dev_network () ? period * 15 : period * 2;
	cutoff = period * 5;
	syn_cookie_cutoff = std::chrono::seconds (5);
	backup_interval = std::chrono::minutes (5);
	bootstrap_interval = std::chrono::seconds (15 * 60);
	search_pending_interval = network_constants.is_dev_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	peer_interval = search_pending_interval;
	unchecked_cleaning_interval = std::chrono::minutes (30);
	process_confirmed_interval = network_constants.is_dev_network () ? std::chrono::milliseconds (50) : std::chrono::milliseconds (500);
	max_peers_per_ip = network_constants.is_dev_network () ? 10 : 5;
	max_peers_per_subnetwork = max_peers_per_ip * 4;
	max_weight_samples = (network_constants.is_live_network () || network_constants.is_test_network ()) ? 4032 : 288;
	weight_period = 5 * 60; // 5 minutes
}

vban::voting_constants::voting_constants (vban::network_constants & network_constants) :
	max_cache{ network_constants.is_dev_network () ? 256U : 128U * 1024 },
	delay{ network_constants.is_dev_network () ? 1 : 15 }
{
}

vban::portmapping_constants::portmapping_constants (vban::network_constants & network_constants)
{
	lease_duration = std::chrono::seconds (1787); // ~30 minutes
	health_check_period = std::chrono::seconds (53);
}

vban::bootstrap_constants::bootstrap_constants (vban::network_constants & network_constants)
{
	lazy_max_pull_blocks = network_constants.is_dev_network () ? 2 : 512;
	lazy_min_pull_blocks = network_constants.is_dev_network () ? 1 : 32;
	frontier_retry_limit = network_constants.is_dev_network () ? 2 : 16;
	lazy_retry_limit = network_constants.is_dev_network () ? 2 : frontier_retry_limit * 4;
	lazy_destinations_retry_limit = network_constants.is_dev_network () ? 1 : frontier_retry_limit / 4;
	gap_cache_bootstrap_start_interval = network_constants.is_dev_network () ? std::chrono::milliseconds (5) : std::chrono::milliseconds (30 * 1000);
	default_frontiers_age_seconds = network_constants.is_dev_network () ? 1 : 24 * 60 * 60; // 1 second for dev network, 24 hours for live/beta
}

// Create a new random keypair
vban::keypair::keypair ()
{
	random_pool::generate_block (prv.bytes.data (), prv.bytes.size ());
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
vban::keypair::keypair (vban::raw_key && prv_a) :
	prv (std::move (prv_a))
{
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
vban::keypair::keypair (std::string const & prv_a)
{
	[[maybe_unused]] auto error (prv.decode_hex (prv_a));
	debug_assert (!error);
	ed25519_publickey (prv.bytes.data (), pub.bytes.data ());
}

// Serialize a block prefixed with an 8-bit typecode
void vban::serialize_block (vban::stream & stream_a, vban::block const & block_a)
{
	write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

vban::account_info::account_info (vban::block_hash const & head_a, vban::account const & representative_a, vban::block_hash const & open_block_a, vban::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, vban::epoch epoch_a) :
	head (head_a),
	representative (representative_a),
	open_block (open_block_a),
	balance (balance_a),
	modified (modified_a),
	block_count (block_count_a),
	epoch_m (epoch_a)
{
}

bool vban::account_info::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		vban::read (stream_a, head.bytes);
		vban::read (stream_a, representative.bytes);
		vban::read (stream_a, open_block.bytes);
		vban::read (stream_a, balance.bytes);
		vban::read (stream_a, modified);
		vban::read (stream_a, block_count);
		vban::read (stream_a, epoch_m);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool vban::account_info::operator== (vban::account_info const & other_a) const
{
	return head == other_a.head && representative == other_a.representative && open_block == other_a.open_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && epoch () == other_a.epoch ();
}

bool vban::account_info::operator!= (vban::account_info const & other_a) const
{
	return !(*this == other_a);
}

size_t vban::account_info::db_size () const
{
	debug_assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	debug_assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&representative));
	debug_assert (reinterpret_cast<const uint8_t *> (&representative) + sizeof (representative) == reinterpret_cast<const uint8_t *> (&open_block));
	debug_assert (reinterpret_cast<const uint8_t *> (&open_block) + sizeof (open_block) == reinterpret_cast<const uint8_t *> (&balance));
	debug_assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	debug_assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	debug_assert (reinterpret_cast<const uint8_t *> (&block_count) + sizeof (block_count) == reinterpret_cast<const uint8_t *> (&epoch_m));
	return sizeof (head) + sizeof (representative) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count) + sizeof (epoch_m);
}

vban::epoch vban::account_info::epoch () const
{
	return epoch_m;
}

vban::pending_info::pending_info (vban::account const & source_a, vban::amount const & amount_a, vban::epoch epoch_a) :
	source (source_a),
	amount (amount_a),
	epoch (epoch_a)
{
}

bool vban::pending_info::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		vban::read (stream_a, source.bytes);
		vban::read (stream_a, amount.bytes);
		vban::read (stream_a, epoch);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

size_t vban::pending_info::db_size () const
{
	return sizeof (source) + sizeof (amount) + sizeof (epoch);
}

bool vban::pending_info::operator== (vban::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
}

vban::pending_key::pending_key (vban::account const & account_a, vban::block_hash const & hash_a) :
	account (account_a),
	hash (hash_a)
{
}

bool vban::pending_key::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		vban::read (stream_a, account.bytes);
		vban::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool vban::pending_key::operator== (vban::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

vban::account const & vban::pending_key::key () const
{
	return account;
}

vban::unchecked_info::unchecked_info (std::shared_ptr<vban::block> const & block_a, vban::account const & account_a, uint64_t modified_a, vban::signature_verification verified_a, bool confirmed_a) :
	block (block_a),
	account (account_a),
	modified (modified_a),
	verified (verified_a),
	confirmed (confirmed_a)
{
}

void vban::unchecked_info::serialize (vban::stream & stream_a) const
{
	debug_assert (block != nullptr);
	vban::serialize_block (stream_a, *block);
	vban::write (stream_a, account.bytes);
	vban::write (stream_a, modified);
	vban::write (stream_a, verified);
}

bool vban::unchecked_info::deserialize (vban::stream & stream_a)
{
	block = vban::deserialize_block (stream_a);
	bool error (block == nullptr);
	if (!error)
	{
		try
		{
			vban::read (stream_a, account.bytes);
			vban::read (stream_a, modified);
			vban::read (stream_a, verified);
		}
		catch (std::runtime_error const &)
		{
			error = true;
		}
	}
	return error;
}

vban::endpoint_key::endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a) :
	address (address_a), network_port (boost::endian::native_to_big (port_a))
{
}

const std::array<uint8_t, 16> & vban::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t vban::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

vban::confirmation_height_info::confirmation_height_info (uint64_t confirmation_height_a, vban::block_hash const & confirmed_frontier_a) :
	height (confirmation_height_a),
	frontier (confirmed_frontier_a)
{
}

void vban::confirmation_height_info::serialize (vban::stream & stream_a) const
{
	vban::write (stream_a, height);
	vban::write (stream_a, frontier);
}

bool vban::confirmation_height_info::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		vban::read (stream_a, height);
		vban::read (stream_a, frontier);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

vban::block_info::block_info (vban::account const & account_a, vban::amount const & balance_a) :
	account (account_a),
	balance (balance_a)
{
}

bool vban::vote::operator== (vban::vote const & other_a) const
{
	auto blocks_equal (true);
	if (blocks.size () != other_a.blocks.size ())
	{
		blocks_equal = false;
	}
	else
	{
		for (auto i (0); blocks_equal && i < blocks.size (); ++i)
		{
			auto block (blocks[i]);
			auto other_block (other_a.blocks[i]);
			if (block.which () != other_block.which ())
			{
				blocks_equal = false;
			}
			else if (block.which ())
			{
				if (boost::get<vban::block_hash> (block) != boost::get<vban::block_hash> (other_block))
				{
					blocks_equal = false;
				}
			}
			else
			{
				if (!(*boost::get<std::shared_ptr<vban::block>> (block) == *boost::get<std::shared_ptr<vban::block>> (other_block)))
				{
					blocks_equal = false;
				}
			}
		}
	}
	return timestamp == other_a.timestamp && blocks_equal && account == other_a.account && signature == other_a.signature;
}

bool vban::vote::operator!= (vban::vote const & other_a) const
{
	return !(*this == other_a);
}

void vban::vote::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("account", account.to_account ());
	tree.put ("signature", signature.number ());
	tree.put ("sequence", std::to_string (timestamp));
	tree.put ("timestamp", std::to_string (timestamp));
	boost::property_tree::ptree blocks_tree;
	for (auto block : blocks)
	{
		boost::property_tree::ptree entry;
		if (block.which ())
		{
			entry.put ("", boost::get<vban::block_hash> (block).to_string ());
		}
		else
		{
			entry.put ("", boost::get<std::shared_ptr<vban::block>> (block)->hash ().to_string ());
		}
		blocks_tree.push_back (std::make_pair ("", entry));
	}
	tree.add_child ("blocks", blocks_tree);
}

std::string vban::vote::to_json () const
{
	std::stringstream stream;
	boost::property_tree::ptree tree;
	serialize_json (tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

vban::vote::vote (vban::vote const & other_a) :
	timestamp{ other_a.timestamp },
	blocks (other_a.blocks),
	account (other_a.account),
	signature (other_a.signature)
{
}

vban::vote::vote (bool & error_a, vban::stream & stream_a, vban::block_uniquer * uniquer_a)
{
	error_a = deserialize (stream_a, uniquer_a);
}

vban::vote::vote (bool & error_a, vban::stream & stream_a, vban::block_type type_a, vban::block_uniquer * uniquer_a)
{
	try
	{
		vban::read (stream_a, account.bytes);
		vban::read (stream_a, signature.bytes);
		vban::read (stream_a, timestamp);

		while (stream_a.in_avail () > 0)
		{
			if (type_a == vban::block_type::not_a_block)
			{
				vban::block_hash block_hash;
				vban::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				auto block (vban::deserialize_block (stream_a, type_a, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is null");
				}
				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}

	if (blocks.empty ())
	{
		error_a = true;
	}
}

vban::vote::vote (vban::account const & account_a, vban::raw_key const & prv_a, uint64_t timestamp_a, std::shared_ptr<vban::block> const & block_a) :
	timestamp{ timestamp_a },
	blocks (1, block_a),
	account (account_a),
	signature (vban::sign_message (prv_a, account_a, hash ()))
{
}

vban::vote::vote (vban::account const & account_a, vban::raw_key const & prv_a, uint64_t timestamp_a, std::vector<vban::block_hash> const & blocks_a) :
	timestamp{ timestamp_a },
	account (account_a)
{
	debug_assert (!blocks_a.empty ());
	debug_assert (blocks_a.size () <= 12);
	blocks.reserve (blocks_a.size ());
	std::copy (blocks_a.cbegin (), blocks_a.cend (), std::back_inserter (blocks));
	signature = vban::sign_message (prv_a, account_a, hash ());
}

std::string vban::vote::hashes_string () const
{
	std::string result;
	for (auto hash : *this)
	{
		result += hash.to_string ();
		result += ", ";
	}
	return result;
}

const std::string vban::vote::hash_prefix = "vote ";

vban::block_hash vban::vote::hash () const
{
	vban::block_hash result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	if (blocks.size () > 1 || (!blocks.empty () && blocks.front ().which ()))
	{
		blake2b_update (&hash, hash_prefix.data (), hash_prefix.size ());
	}
	for (auto block_hash : *this)
	{
		blake2b_update (&hash, block_hash.bytes.data (), sizeof (block_hash.bytes));
	}
	union
	{
		uint64_t qword;
		std::array<uint8_t, 8> bytes;
	};
	qword = timestamp;
	blake2b_update (&hash, bytes.data (), sizeof (bytes));
	blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
	return result;
}

vban::block_hash vban::vote::full_hash () const
{
	vban::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ().bytes));
	blake2b_update (&state, account.bytes.data (), sizeof (account.bytes.data ()));
	blake2b_update (&state, signature.bytes.data (), sizeof (signature.bytes.data ()));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

void vban::vote::serialize (vban::stream & stream_a, vban::block_type type) const
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, timestamp);
	for (auto const & block : blocks)
	{
		if (block.which ())
		{
			debug_assert (type == vban::block_type::not_a_block);
			write (stream_a, boost::get<vban::block_hash> (block));
		}
		else
		{
			if (type == vban::block_type::not_a_block)
			{
				write (stream_a, boost::get<std::shared_ptr<vban::block>> (block)->hash ());
			}
			else
			{
				boost::get<std::shared_ptr<vban::block>> (block)->serialize (stream_a);
			}
		}
	}
}

void vban::vote::serialize (vban::stream & stream_a) const
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, timestamp);
	for (auto const & block : blocks)
	{
		if (block.which ())
		{
			write (stream_a, vban::block_type::not_a_block);
			write (stream_a, boost::get<vban::block_hash> (block));
		}
		else
		{
			vban::serialize_block (stream_a, *boost::get<std::shared_ptr<vban::block>> (block));
		}
	}
}

bool vban::vote::deserialize (vban::stream & stream_a, vban::block_uniquer * uniquer_a)
{
	auto error (false);
	try
	{
		vban::read (stream_a, account);
		vban::read (stream_a, signature);
		vban::read (stream_a, timestamp);

		vban::block_type type;

		while (true)
		{
			if (vban::try_read (stream_a, type))
			{
				// Reached the end of the stream
				break;
			}

			if (type == vban::block_type::not_a_block)
			{
				vban::block_hash block_hash;
				vban::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				auto block (vban::deserialize_block (stream_a, type, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is empty");
				}

				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	if (blocks.empty ())
	{
		error = true;
	}

	return error;
}

bool vban::vote::validate () const
{
	return vban::validate_message (account, hash (), signature);
}

vban::block_hash vban::iterate_vote_blocks_as_hash::operator() (boost::variant<std::shared_ptr<vban::block>, vban::block_hash> const & item) const
{
	vban::block_hash result;
	if (item.which ())
	{
		result = boost::get<vban::block_hash> (item);
	}
	else
	{
		result = boost::get<std::shared_ptr<vban::block>> (item)->hash ();
	}
	return result;
}

boost::transform_iterator<vban::iterate_vote_blocks_as_hash, vban::vote_blocks_vec_iter> vban::vote::begin () const
{
	return boost::transform_iterator<vban::iterate_vote_blocks_as_hash, vban::vote_blocks_vec_iter> (blocks.begin (), vban::iterate_vote_blocks_as_hash ());
}

boost::transform_iterator<vban::iterate_vote_blocks_as_hash, vban::vote_blocks_vec_iter> vban::vote::end () const
{
	return boost::transform_iterator<vban::iterate_vote_blocks_as_hash, vban::vote_blocks_vec_iter> (blocks.end (), vban::iterate_vote_blocks_as_hash ());
}

vban::vote_uniquer::vote_uniquer (vban::block_uniquer & uniquer_a) :
	uniquer (uniquer_a)
{
}

std::shared_ptr<vban::vote> vban::vote_uniquer::unique (std::shared_ptr<vban::vote> const & vote_a)
{
	auto result (vote_a);
	if (result != nullptr && !result->blocks.empty ())
	{
		if (!result->blocks.front ().which ())
		{
			result->blocks.front () = uniquer.unique (boost::get<std::shared_ptr<vban::block>> (result->blocks.front ()));
		}
		vban::block_hash key (vote_a->full_hash ());
		vban::lock_guard<vban::mutex> lock (mutex);
		auto & existing (votes[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = vote_a;
		}

		release_assert (std::numeric_limits<CryptoPP::word32>::max () > votes.size ());
		for (auto i (0); i < cleanup_count && !votes.empty (); ++i)
		{
			auto random_offset = vban::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (votes.size () - 1));

			auto existing (std::next (votes.begin (), random_offset));
			if (existing == votes.end ())
			{
				existing = votes.begin ();
			}
			if (existing != votes.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					votes.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t vban::vote_uniquer::size ()
{
	vban::lock_guard<vban::mutex> lock (mutex);
	return votes.size ();
}

std::unique_ptr<vban::container_info_component> vban::collect_container_info (vote_uniquer & vote_uniquer, std::string const & name)
{
	auto count = vote_uniquer.size ();
	auto sizeof_element = sizeof (vote_uniquer::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "votes", count, sizeof_element }));
	return composite;
}

vban::genesis::genesis ()
{
	static vban::network_params network_params;
	open = parse_block_from_genesis_data (network_params.ledger.genesis_block);
	debug_assert (open != nullptr);
}

vban::block_hash vban::genesis::hash () const
{
	return open->hash ();
}

vban::wallet_id vban::random_wallet_id ()
{
	vban::wallet_id wallet_id;
	vban::uint256_union dummy_secret;
	random_pool::generate_block (dummy_secret.bytes.data (), dummy_secret.bytes.size ());
	ed25519_publickey (dummy_secret.bytes.data (), wallet_id.bytes.data ());
	return wallet_id;
}

vban::unchecked_key::unchecked_key (vban::hash_or_account const & previous_a, vban::block_hash const & hash_a) :
	previous (previous_a.hash),
	hash (hash_a)
{
}

vban::unchecked_key::unchecked_key (vban::uint512_union const & union_a) :
	previous (union_a.uint256s[0].number ()),
	hash (union_a.uint256s[1].number ())
{
}

bool vban::unchecked_key::deserialize (vban::stream & stream_a)
{
	auto error (false);
	try
	{
		vban::read (stream_a, previous.bytes);
		vban::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool vban::unchecked_key::operator== (vban::unchecked_key const & other_a) const
{
	return previous == other_a.previous && hash == other_a.hash;
}

vban::block_hash const & vban::unchecked_key::key () const
{
	return previous;
}

void vban::generate_cache::enable_all ()
{
	reps = true;
	cemented_count = true;
	unchecked_count = true;
	account_count = true;
}
