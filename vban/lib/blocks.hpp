#pragma once

#include <vban/crypto/blake2/blake2.h>
#include <vban/lib/epoch.hpp>
#include <vban/lib/errors.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/lib/optional_ptr.hpp>
#include <vban/lib/stream.hpp>
#include <vban/lib/utility.hpp>
#include <vban/lib/work.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <unordered_map>

namespace vban
{
class block_visitor;
class mutable_block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};
class block_details
{
	static_assert (std::is_same<std::underlying_type<vban::epoch>::type, uint8_t> (), "Epoch enum is not the proper type");
	static_assert (static_cast<uint8_t> (vban::epoch::max) < (1 << 5), "Epoch max is too large for the sideband");

public:
	block_details () = default;
	block_details (vban::epoch const epoch_a, bool const is_send_a, bool const is_receive_a, bool const is_epoch_a);
	static constexpr size_t size ()
	{
		return 1;
	}
	bool operator== (block_details const & other_a) const;
	void serialize (vban::stream &) const;
	bool deserialize (vban::stream &);
	vban::epoch epoch{ vban::epoch::epoch_0 };
	bool is_send{ false };
	bool is_receive{ false };
	bool is_epoch{ false };

private:
	uint8_t packed () const;
	void unpack (uint8_t);
};

std::string state_subtype (vban::block_details const);

class block_sideband final
{
public:
	block_sideband () = default;
	block_sideband (vban::account const &, vban::block_hash const &, vban::amount const &, uint64_t const, uint64_t const, vban::block_details const &, vban::epoch const source_epoch_a);
	block_sideband (vban::account const &, vban::block_hash const &, vban::amount const &, uint64_t const, uint64_t const, vban::epoch const epoch_a, bool const is_send, bool const is_receive, bool const is_epoch, vban::epoch const source_epoch_a);
	void serialize (vban::stream &, vban::block_type) const;
	bool deserialize (vban::stream &, vban::block_type);
	static size_t size (vban::block_type);
	vban::block_hash successor{ 0 };
	vban::account account{ 0 };
	vban::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	vban::block_details details;
	vban::epoch source_epoch{ vban::epoch::epoch_0 };
};
class block
{
public:
	// Return a digest of the hashables in this block.
	vban::block_hash const & hash () const;
	// Return a digest of hashables and non-hashables in this block.
	vban::block_hash full_hash () const;
	vban::block_sideband const & sideband () const;
	void sideband_set (vban::block_sideband const &);
	bool has_sideband () const;
	std::string to_json () const;
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	virtual vban::account const & account () const;
	// Previous block in account's chain, zero for open block
	virtual vban::block_hash const & previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual vban::block_hash const & source () const;
	// Destination account for send blocks, zero otherwise.
	virtual vban::account const & destination () const;
	// Previous block or account number for open blocks
	virtual vban::root const & root () const = 0;
	// Qualified root value based on previous() and root()
	virtual vban::qualified_root qualified_root () const;
	// Link field for state blocks, zero otherwise.
	virtual vban::link const & link () const;
	virtual vban::account const & representative () const;
	virtual vban::amount const & balance () const;
	virtual void serialize (vban::stream &) const = 0;
	virtual void serialize_json (std::string &, bool = false) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (vban::block_visitor &) const = 0;
	virtual void visit (vban::mutable_block_visitor &) = 0;
	virtual bool operator== (vban::block const &) const = 0;
	virtual vban::block_type type () const = 0;
	virtual vban::signature const & block_signature () const = 0;
	virtual void signature_set (vban::signature const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (vban::block const &) const = 0;
	static size_t size (vban::block_type);
	virtual vban::work_version work_version () const;
	uint64_t difficulty () const;
	// If there are any changes to the hashables, call this to update the cached hash
	void refresh ();

protected:
	mutable vban::block_hash cached_hash{ 0 };
	/**
	 * Contextual details about a block, some fields may or may not be set depending on block type.
	 * This field is set via sideband_set in ledger processing or deserializing blocks from the database.
	 * Otherwise it may be null (for example, an old block or fork).
	 */
	vban::optional_ptr<vban::block_sideband> sideband_m;

private:
	vban::block_hash generate_hash () const;
};
class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (vban::block_hash const &, vban::account const &, vban::amount const &);
	send_hashables (bool &, vban::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	vban::block_hash previous;
	vban::account destination;
	vban::amount balance;
	static size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};
class send_block : public vban::block
{
public:
	send_block () = default;
	send_block (vban::block_hash const &, vban::account const &, vban::amount const &, vban::raw_key const &, vban::public_key const &, uint64_t);
	send_block (bool &, vban::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using vban::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	vban::block_hash const & previous () const override;
	vban::account const & destination () const override;
	vban::root const & root () const override;
	vban::amount const & balance () const override;
	void serialize (vban::stream &) const override;
	bool deserialize (vban::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (vban::block_visitor &) const override;
	void visit (vban::mutable_block_visitor &) override;
	vban::block_type type () const override;
	vban::signature const & block_signature () const override;
	void signature_set (vban::signature const &) override;
	bool operator== (vban::block const &) const override;
	bool operator== (vban::send_block const &) const;
	bool valid_predecessor (vban::block const &) const override;
	send_hashables hashables;
	vban::signature signature;
	uint64_t work;
	static size_t constexpr size = vban::send_hashables::size + sizeof (signature) + sizeof (work);
};
class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (vban::block_hash const &, vban::block_hash const &);
	receive_hashables (bool &, vban::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	vban::block_hash previous;
	vban::block_hash source;
	static size_t constexpr size = sizeof (previous) + sizeof (source);
};
class receive_block : public vban::block
{
public:
	receive_block () = default;
	receive_block (vban::block_hash const &, vban::block_hash const &, vban::raw_key const &, vban::public_key const &, uint64_t);
	receive_block (bool &, vban::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using vban::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	vban::block_hash const & previous () const override;
	vban::block_hash const & source () const override;
	vban::root const & root () const override;
	void serialize (vban::stream &) const override;
	bool deserialize (vban::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (vban::block_visitor &) const override;
	void visit (vban::mutable_block_visitor &) override;
	vban::block_type type () const override;
	vban::signature const & block_signature () const override;
	void signature_set (vban::signature const &) override;
	bool operator== (vban::block const &) const override;
	bool operator== (vban::receive_block const &) const;
	bool valid_predecessor (vban::block const &) const override;
	receive_hashables hashables;
	vban::signature signature;
	uint64_t work;
	static size_t constexpr size = vban::receive_hashables::size + sizeof (signature) + sizeof (work);
};
class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (vban::block_hash const &, vban::account const &, vban::account const &);
	open_hashables (bool &, vban::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	vban::block_hash source;
	vban::account representative;
	vban::account account;
	static size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};
class open_block : public vban::block
{
public:
	open_block () = default;
	open_block (vban::block_hash const &, vban::account const &, vban::account const &, vban::raw_key const &, vban::public_key const &, uint64_t);
	open_block (vban::block_hash const &, vban::account const &, vban::account const &, std::nullptr_t);
	open_block (bool &, vban::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using vban::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	vban::block_hash const & previous () const override;
	vban::account const & account () const override;
	vban::block_hash const & source () const override;
	vban::root const & root () const override;
	vban::account const & representative () const override;
	void serialize (vban::stream &) const override;
	bool deserialize (vban::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (vban::block_visitor &) const override;
	void visit (vban::mutable_block_visitor &) override;
	vban::block_type type () const override;
	vban::signature const & block_signature () const override;
	void signature_set (vban::signature const &) override;
	bool operator== (vban::block const &) const override;
	bool operator== (vban::open_block const &) const;
	bool valid_predecessor (vban::block const &) const override;
	vban::open_hashables hashables;
	vban::signature signature;
	uint64_t work;
	static size_t constexpr size = vban::open_hashables::size + sizeof (signature) + sizeof (work);
};
class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (vban::block_hash const &, vban::account const &);
	change_hashables (bool &, vban::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	vban::block_hash previous;
	vban::account representative;
	static size_t constexpr size = sizeof (previous) + sizeof (representative);
};
class change_block : public vban::block
{
public:
	change_block () = default;
	change_block (vban::block_hash const &, vban::account const &, vban::raw_key const &, vban::public_key const &, uint64_t);
	change_block (bool &, vban::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using vban::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	vban::block_hash const & previous () const override;
	vban::root const & root () const override;
	vban::account const & representative () const override;
	void serialize (vban::stream &) const override;
	bool deserialize (vban::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (vban::block_visitor &) const override;
	void visit (vban::mutable_block_visitor &) override;
	vban::block_type type () const override;
	vban::signature const & block_signature () const override;
	void signature_set (vban::signature const &) override;
	bool operator== (vban::block const &) const override;
	bool operator== (vban::change_block const &) const;
	bool valid_predecessor (vban::block const &) const override;
	vban::change_hashables hashables;
	vban::signature signature;
	uint64_t work;
	static size_t constexpr size = vban::change_hashables::size + sizeof (signature) + sizeof (work);
};
class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (vban::account const &, vban::block_hash const &, vban::account const &, vban::amount const &, vban::link const &);
	state_hashables (bool &, vban::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	vban::account account;
	// Previous transaction in this chain
	vban::block_hash previous;
	// Representative of this account
	vban::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	vban::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	vban::link link;
	// Serialized size
	static size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};
class state_block : public vban::block
{
public:
	state_block () = default;
	state_block (vban::account const &, vban::block_hash const &, vban::account const &, vban::amount const &, vban::link const &, vban::raw_key const &, vban::public_key const &, uint64_t);
	state_block (bool &, vban::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using vban::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	vban::block_hash const & previous () const override;
	vban::account const & account () const override;
	vban::root const & root () const override;
	vban::link const & link () const override;
	vban::account const & representative () const override;
	vban::amount const & balance () const override;
	void serialize (vban::stream &) const override;
	bool deserialize (vban::stream &);
	void serialize_json (std::string &, bool = false) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (vban::block_visitor &) const override;
	void visit (vban::mutable_block_visitor &) override;
	vban::block_type type () const override;
	vban::signature const & block_signature () const override;
	void signature_set (vban::signature const &) override;
	bool operator== (vban::block const &) const override;
	bool operator== (vban::state_block const &) const;
	bool valid_predecessor (vban::block const &) const override;
	vban::state_hashables hashables;
	vban::signature signature;
	uint64_t work;
	static size_t constexpr size = vban::state_hashables::size + sizeof (signature) + sizeof (work);
};
class block_visitor
{
public:
	virtual void send_block (vban::send_block const &) = 0;
	virtual void receive_block (vban::receive_block const &) = 0;
	virtual void open_block (vban::open_block const &) = 0;
	virtual void change_block (vban::change_block const &) = 0;
	virtual void state_block (vban::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
class mutable_block_visitor
{
public:
	virtual void send_block (vban::send_block &) = 0;
	virtual void receive_block (vban::receive_block &) = 0;
	virtual void open_block (vban::open_block &) = 0;
	virtual void change_block (vban::change_block &) = 0;
	virtual void state_block (vban::state_block &) = 0;
	virtual ~mutable_block_visitor () = default;
};
/**
 * This class serves to find and return unique variants of a block in order to minimize memory usage
 */
class block_uniquer
{
public:
	using value_type = std::pair<const vban::uint256_union, std::weak_ptr<vban::block>>;

	std::shared_ptr<vban::block> unique (std::shared_ptr<vban::block> const &);
	size_t size ();

private:
	vban::mutex mutex{ mutex_identifier (mutexes::block_uniquer) };
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> blocks;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<container_info_component> collect_container_info (block_uniquer & block_uniquer, std::string const & name);

std::shared_ptr<vban::block> deserialize_block (vban::stream &);
std::shared_ptr<vban::block> deserialize_block (vban::stream &, vban::block_type, vban::block_uniquer * = nullptr);
std::shared_ptr<vban::block> deserialize_block_json (boost::property_tree::ptree const &, vban::block_uniquer * = nullptr);
void serialize_block (vban::stream &, vban::block const &);
void block_memory_pool_purge ();
}
