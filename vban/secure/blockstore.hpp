#pragma once

#include <vban/crypto_lib/random_pool.hpp>
#include <vban/lib/diagnosticsconfig.hpp>
#include <vban/lib/lmdbconfig.hpp>
#include <vban/lib/logger_mt.hpp>
#include <vban/lib/memory.hpp>
#include <vban/lib/rocksdbconfig.hpp>
#include <vban/secure/buffer.hpp>
#include <vban/secure/common.hpp>
#include <vban/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <stack>

namespace vban
{
// Move to versioning with a specific version if required for a future upgrade
template <typename T>
class block_w_sideband_v18
{
public:
	std::shared_ptr<T> block;
	vban::block_sideband_v18 sideband;
};

class block_w_sideband
{
public:
	std::shared_ptr<vban::block> block;
	vban::block_sideband sideband;
};

/**
 * Encapsulates database specific container
 */
template <typename Val>
class db_val
{
public:
	db_val (Val const & value_a) :
		value (value_a)
	{
	}

	db_val () :
		db_val (0, nullptr)
	{
	}

	db_val (std::nullptr_t) :
		db_val (0, this)
	{
	}

	db_val (vban::uint128_union const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::uint128_union *> (&val_a))
	{
	}

	db_val (vban::uint256_union const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::uint256_union *> (&val_a))
	{
	}

	db_val (vban::uint512_union const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::uint512_union *> (&val_a))
	{
	}

	db_val (vban::qualified_root const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::qualified_root *> (&val_a))
	{
	}

	db_val (vban::account_info const & val_a) :
		db_val (val_a.db_size (), const_cast<vban::account_info *> (&val_a))
	{
	}

	db_val (vban::account_info_v14 const & val_a) :
		db_val (val_a.db_size (), const_cast<vban::account_info_v14 *> (&val_a))
	{
	}

	db_val (vban::pending_info const & val_a) :
		db_val (val_a.db_size (), const_cast<vban::pending_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<vban::pending_info>::value, "Standard layout is required");
	}

	db_val (vban::pending_info_v14 const & val_a) :
		db_val (val_a.db_size (), const_cast<vban::pending_info_v14 *> (&val_a))
	{
		static_assert (std::is_standard_layout<vban::pending_info_v14>::value, "Standard layout is required");
	}

	db_val (vban::pending_key const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::pending_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<vban::pending_key>::value, "Standard layout is required");
	}

	db_val (vban::unchecked_info const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			vban::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (vban::unchecked_key const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::unchecked_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<vban::unchecked_key>::value, "Standard layout is required");
	}

	db_val (vban::confirmation_height_info const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			vban::vectorstream stream (*buffer);
			val_a.serialize (stream);
		}
		convert_buffer_to_value ();
	}

	db_val (vban::block_info const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::block_info *> (&val_a))
	{
		static_assert (std::is_standard_layout<vban::block_info>::value, "Standard layout is required");
	}

	db_val (vban::endpoint_key const & val_a) :
		db_val (sizeof (val_a), const_cast<vban::endpoint_key *> (&val_a))
	{
		static_assert (std::is_standard_layout<vban::endpoint_key>::value, "Standard layout is required");
	}

	db_val (std::shared_ptr<vban::block> const & val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			vban::vectorstream stream (*buffer);
			vban::serialize_block (stream, *val_a);
		}
		convert_buffer_to_value ();
	}

	db_val (uint64_t val_a) :
		buffer (std::make_shared<std::vector<uint8_t>> ())
	{
		{
			boost::endian::native_to_big_inplace (val_a);
			vban::vectorstream stream (*buffer);
			vban::write (stream, val_a);
		}
		convert_buffer_to_value ();
	}

	explicit operator vban::account_info () const
	{
		vban::account_info result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator vban::account_info_v14 () const
	{
		vban::account_info_v14 result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator vban::block_info () const
	{
		vban::block_info result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (vban::block_info::account) + sizeof (vban::block_info::balance) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator vban::pending_info_v14 () const
	{
		vban::pending_info_v14 result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator vban::pending_info () const
	{
		vban::pending_info result;
		debug_assert (size () == result.db_size ());
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator vban::pending_key () const
	{
		vban::pending_key result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (vban::pending_key::account) + sizeof (vban::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator vban::confirmation_height_info () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		vban::confirmation_height_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator vban::unchecked_info () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		vban::unchecked_info result;
		bool error (result.deserialize (stream));
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator vban::unchecked_key () const
	{
		vban::unchecked_key result;
		debug_assert (size () == sizeof (result));
		static_assert (sizeof (vban::unchecked_key::previous) + sizeof (vban::pending_key::hash) == sizeof (result), "Packed class");
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	explicit operator vban::uint128_union () const
	{
		return convert<vban::uint128_union> ();
	}

	explicit operator vban::amount () const
	{
		return convert<vban::amount> ();
	}

	explicit operator vban::block_hash () const
	{
		return convert<vban::block_hash> ();
	}

	explicit operator vban::public_key () const
	{
		return convert<vban::public_key> ();
	}

	explicit operator vban::qualified_root () const
	{
		return convert<vban::qualified_root> ();
	}

	explicit operator vban::uint256_union () const
	{
		return convert<vban::uint256_union> ();
	}

	explicit operator vban::uint512_union () const
	{
		return convert<vban::uint512_union> ();
	}

	explicit operator std::array<char, 64> () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::array<char, 64> result;
		auto error = vban::try_read (stream, result);
		(void)error;
		debug_assert (!error);
		return result;
	}

	explicit operator vban::endpoint_key () const
	{
		vban::endpoint_key result;
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
		return result;
	}

	template <class Block>
	explicit operator block_w_sideband_v18<Block> () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		block_w_sideband_v18<Block> block_w_sideband;
		block_w_sideband.block = std::make_shared<Block> (error, stream);
		release_assert (!error);

		error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
		release_assert (!error);

		return block_w_sideband;
	}

	explicit operator block_w_sideband () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		vban::block_w_sideband block_w_sideband;
		block_w_sideband.block = (vban::deserialize_block (stream));
		auto error = block_w_sideband.sideband.deserialize (stream, block_w_sideband.block->type ());
		release_assert (!error);
		block_w_sideband.block->sideband_set (block_w_sideband.sideband);
		return block_w_sideband;
	}

	explicit operator state_block_w_sideband_v14 () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		vban::state_block_w_sideband_v14 block_w_sideband;
		block_w_sideband.state_block = std::make_shared<vban::state_block> (error, stream);
		debug_assert (!error);

		block_w_sideband.sideband.type = vban::block_type::state;
		error = block_w_sideband.sideband.deserialize (stream);
		debug_assert (!error);

		return block_w_sideband;
	}

	explicit operator std::nullptr_t () const
	{
		return nullptr;
	}

	explicit operator vban::no_value () const
	{
		return no_value::dummy;
	}

	explicit operator std::shared_ptr<vban::block> () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		std::shared_ptr<vban::block> result (vban::deserialize_block (stream));
		return result;
	}

	template <typename Block>
	std::shared_ptr<Block> convert_to_block () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (std::make_shared<Block> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator std::shared_ptr<vban::send_block> () const
	{
		return convert_to_block<vban::send_block> ();
	}

	explicit operator std::shared_ptr<vban::receive_block> () const
	{
		return convert_to_block<vban::receive_block> ();
	}

	explicit operator std::shared_ptr<vban::open_block> () const
	{
		return convert_to_block<vban::open_block> ();
	}

	explicit operator std::shared_ptr<vban::change_block> () const
	{
		return convert_to_block<vban::change_block> ();
	}

	explicit operator std::shared_ptr<vban::state_block> () const
	{
		return convert_to_block<vban::state_block> ();
	}

	explicit operator std::shared_ptr<vban::vote> () const
	{
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (false);
		auto result (vban::make_shared<vban::vote> (error, stream));
		debug_assert (!error);
		return result;
	}

	explicit operator uint64_t () const
	{
		uint64_t result;
		vban::bufferstream stream (reinterpret_cast<uint8_t const *> (data ()), size ());
		auto error (vban::try_read (stream, result));
		(void)error;
		debug_assert (!error);
		boost::endian::big_to_native_inplace (result);
		return result;
	}

	operator Val * () const
	{
		// Allow passing a temporary to a non-c++ function which doesn't have constness
		return const_cast<Val *> (&value);
	}

	operator Val const & () const
	{
		return value;
	}

	// Must be specialized
	void * data () const;
	size_t size () const;
	db_val (size_t size_a, void * data_a);
	void convert_buffer_to_value ();

	Val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;

private:
	template <typename T>
	T convert () const
	{
		T result;
		debug_assert (size () == sizeof (result));
		std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
		return result;
	}
};

class transaction;
class block_store;

/**
 * Determine the representative for this block
 */
class representative_visitor final : public vban::block_visitor
{
public:
	representative_visitor (vban::transaction const & transaction_a, vban::block_store & store_a);
	~representative_visitor () = default;
	void compute (vban::block_hash const & hash_a);
	void send_block (vban::send_block const & block_a) override;
	void receive_block (vban::receive_block const & block_a) override;
	void open_block (vban::open_block const & block_a) override;
	void change_block (vban::change_block const & block_a) override;
	void state_block (vban::state_block const & block_a) override;
	vban::transaction const & transaction;
	vban::block_store & store;
	vban::block_hash current;
	vban::block_hash result;
};
template <typename T, typename U>
class store_iterator_impl
{
public:
	virtual ~store_iterator_impl () = default;
	virtual vban::store_iterator_impl<T, U> & operator++ () = 0;
	virtual vban::store_iterator_impl<T, U> & operator-- () = 0;
	virtual bool operator== (vban::store_iterator_impl<T, U> const & other_a) const = 0;
	virtual bool is_end_sentinal () const = 0;
	virtual void fill (std::pair<T, U> &) const = 0;
	vban::store_iterator_impl<T, U> & operator= (vban::store_iterator_impl<T, U> const &) = delete;
	bool operator== (vban::store_iterator_impl<T, U> const * other_a) const
	{
		return (other_a != nullptr && *this == *other_a) || (other_a == nullptr && is_end_sentinal ());
	}
	bool operator!= (vban::store_iterator_impl<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}
};
/**
 * Iterates the key/value pairs of a transaction
 */
template <typename T, typename U>
class store_iterator final
{
public:
	store_iterator (std::nullptr_t)
	{
	}
	store_iterator (std::unique_ptr<vban::store_iterator_impl<T, U>> impl_a) :
		impl (std::move (impl_a))
	{
		impl->fill (current);
	}
	store_iterator (vban::store_iterator<T, U> && other_a) :
		current (std::move (other_a.current)),
		impl (std::move (other_a.impl))
	{
	}
	vban::store_iterator<T, U> & operator++ ()
	{
		++*impl;
		impl->fill (current);
		return *this;
	}
	vban::store_iterator<T, U> & operator-- ()
	{
		--*impl;
		impl->fill (current);
		return *this;
	}
	vban::store_iterator<T, U> & operator= (vban::store_iterator<T, U> && other_a) noexcept
	{
		impl = std::move (other_a.impl);
		current = std::move (other_a.current);
		return *this;
	}
	vban::store_iterator<T, U> & operator= (vban::store_iterator<T, U> const &) = delete;
	std::pair<T, U> * operator-> ()
	{
		return &current;
	}
	bool operator== (vban::store_iterator<T, U> const & other_a) const
	{
		return (impl == nullptr && other_a.impl == nullptr) || (impl != nullptr && *impl == other_a.impl.get ()) || (other_a.impl != nullptr && *other_a.impl == impl.get ());
	}
	bool operator!= (vban::store_iterator<T, U> const & other_a) const
	{
		return !(*this == other_a);
	}

private:
	std::pair<T, U> current;
	std::unique_ptr<vban::store_iterator_impl<T, U>> impl;
};

// Keep this in alphabetical order
enum class tables
{
	accounts,
	blocks,
	confirmation_height,
	default_unused, // RocksDB only
	final_votes,
	frontiers,
	meta,
	online_weight,
	peers,
	pending,
	pruned,
	unchecked,
	vote
};

class transaction_impl
{
public:
	virtual ~transaction_impl () = default;
	virtual void * get_handle () const = 0;
};

class read_transaction_impl : public transaction_impl
{
public:
	virtual void reset () = 0;
	virtual void renew () = 0;
};

class write_transaction_impl : public transaction_impl
{
public:
	virtual void commit () = 0;
	virtual void renew () = 0;
	virtual bool contains (vban::tables table_a) const = 0;
};

class transaction
{
public:
	virtual ~transaction () = default;
	virtual void * get_handle () const = 0;
};

/**
 * RAII wrapper of a read MDB_txn where the constructor starts the transaction
 * and the destructor aborts it.
 */
class read_transaction final : public transaction
{
public:
	explicit read_transaction (std::unique_ptr<vban::read_transaction_impl> read_transaction_impl);
	void * get_handle () const override;
	void reset () const;
	void renew () const;
	void refresh () const;

private:
	std::unique_ptr<vban::read_transaction_impl> impl;
};

/**
 * RAII wrapper of a read-write MDB_txn where the constructor starts the transaction
 * and the destructor commits it.
 */
class write_transaction final : public transaction
{
public:
	explicit write_transaction (std::unique_ptr<vban::write_transaction_impl> write_transaction_impl);
	void * get_handle () const override;
	void commit ();
	void renew ();
	void refresh ();
	bool contains (vban::tables table_a) const;

private:
	std::unique_ptr<vban::write_transaction_impl> impl;
};

class ledger_cache;

/**
 * Manages block storage and iteration
 */
class block_store
{
public:
	virtual ~block_store () = default;
	virtual void initialize (vban::write_transaction const &, vban::genesis const &, vban::ledger_cache &) = 0;
	virtual void block_put (vban::write_transaction const &, vban::block_hash const &, vban::block const &) = 0;
	virtual void block_raw_put (vban::write_transaction const &, std::vector<uint8_t> const &, vban::block_hash const &) = 0;
	virtual vban::block_hash block_successor (vban::transaction const &, vban::block_hash const &) const = 0;
	virtual void block_successor_clear (vban::write_transaction const &, vban::block_hash const &) = 0;
	virtual std::shared_ptr<vban::block> block_get (vban::transaction const &, vban::block_hash const &) const = 0;
	virtual std::shared_ptr<vban::block> block_get_no_sideband (vban::transaction const &, vban::block_hash const &) const = 0;
	virtual std::shared_ptr<vban::block> block_random (vban::transaction const &) = 0;
	virtual void block_del (vban::write_transaction const &, vban::block_hash const &) = 0;
	virtual bool block_exists (vban::transaction const &, vban::block_hash const &) = 0;
	virtual uint64_t block_count (vban::transaction const &) = 0;
	virtual bool root_exists (vban::transaction const &, vban::root const &) = 0;
	virtual vban::account block_account (vban::transaction const &, vban::block_hash const &) const = 0;
	virtual vban::account block_account_calculated (vban::block const &) const = 0;
	virtual vban::store_iterator<vban::block_hash, block_w_sideband> blocks_begin (vban::transaction const &, vban::block_hash const &) const = 0;
	virtual vban::store_iterator<vban::block_hash, block_w_sideband> blocks_begin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<vban::block_hash, block_w_sideband> blocks_end () const = 0;

	virtual void frontier_put (vban::write_transaction const &, vban::block_hash const &, vban::account const &) = 0;
	virtual vban::account frontier_get (vban::transaction const &, vban::block_hash const &) const = 0;
	virtual void frontier_del (vban::write_transaction const &, vban::block_hash const &) = 0;
	virtual vban::store_iterator<vban::block_hash, vban::account> frontiers_begin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<vban::block_hash, vban::account> frontiers_begin (vban::transaction const &, vban::block_hash const &) const = 0;
	virtual vban::store_iterator<vban::block_hash, vban::account> frontiers_end () const = 0;

	virtual void account_put (vban::write_transaction const &, vban::account const &, vban::account_info const &) = 0;
	virtual bool account_get (vban::transaction const &, vban::account const &, vban::account_info &) = 0;
	virtual void account_del (vban::write_transaction const &, vban::account const &) = 0;
	virtual bool account_exists (vban::transaction const &, vban::account const &) = 0;
	virtual size_t account_count (vban::transaction const &) = 0;
	virtual vban::store_iterator<vban::account, vban::account_info> accounts_begin (vban::transaction const &, vban::account const &) const = 0;
	virtual vban::store_iterator<vban::account, vban::account_info> accounts_begin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<vban::account, vban::account_info> accounts_rbegin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<vban::account, vban::account_info> accounts_end () const = 0;

	virtual void pending_put (vban::write_transaction const &, vban::pending_key const &, vban::pending_info const &) = 0;
	virtual void pending_del (vban::write_transaction const &, vban::pending_key const &) = 0;
	virtual bool pending_get (vban::transaction const &, vban::pending_key const &, vban::pending_info &) = 0;
	virtual bool pending_exists (vban::transaction const &, vban::pending_key const &) = 0;
	virtual bool pending_any (vban::transaction const &, vban::account const &) = 0;
	virtual vban::store_iterator<vban::pending_key, vban::pending_info> pending_begin (vban::transaction const &, vban::pending_key const &) const = 0;
	virtual vban::store_iterator<vban::pending_key, vban::pending_info> pending_begin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<vban::pending_key, vban::pending_info> pending_end () const = 0;

	virtual vban::uint256_t block_balance (vban::transaction const &, vban::block_hash const &) = 0;
	virtual vban::uint256_t block_balance_calculated (std::shared_ptr<vban::block> const &) const = 0;
	virtual vban::epoch block_version (vban::transaction const &, vban::block_hash const &) = 0;

	virtual void unchecked_clear (vban::write_transaction const &) = 0;
	virtual void unchecked_put (vban::write_transaction const &, vban::unchecked_key const &, vban::unchecked_info const &) = 0;
	virtual void unchecked_put (vban::write_transaction const &, vban::block_hash const &, std::shared_ptr<vban::block> const &) = 0;
	virtual std::vector<vban::unchecked_info> unchecked_get (vban::transaction const &, vban::block_hash const &) = 0;
	virtual bool unchecked_exists (vban::transaction const & transaction_a, vban::unchecked_key const & unchecked_key_a) = 0;
	virtual void unchecked_del (vban::write_transaction const &, vban::unchecked_key const &) = 0;
	virtual vban::store_iterator<vban::unchecked_key, vban::unchecked_info> unchecked_begin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<vban::unchecked_key, vban::unchecked_info> unchecked_begin (vban::transaction const &, vban::unchecked_key const &) const = 0;
	virtual vban::store_iterator<vban::unchecked_key, vban::unchecked_info> unchecked_end () const = 0;
	virtual size_t unchecked_count (vban::transaction const &) = 0;

	virtual void online_weight_put (vban::write_transaction const &, uint64_t, vban::amount const &) = 0;
	virtual void online_weight_del (vban::write_transaction const &, uint64_t) = 0;
	virtual vban::store_iterator<uint64_t, vban::amount> online_weight_begin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<uint64_t, vban::amount> online_weight_rbegin (vban::transaction const &) const = 0;
	virtual vban::store_iterator<uint64_t, vban::amount> online_weight_end () const = 0;
	virtual size_t online_weight_count (vban::transaction const &) const = 0;
	virtual void online_weight_clear (vban::write_transaction const &) = 0;

	virtual void version_put (vban::write_transaction const &, int) = 0;
	virtual int version_get (vban::transaction const &) const = 0;

	virtual void pruned_put (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a) = 0;
	virtual void pruned_del (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a) = 0;
	virtual bool pruned_exists (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const = 0;
	virtual vban::block_hash pruned_random (vban::transaction const & transaction_a) = 0;
	virtual size_t pruned_count (vban::transaction const & transaction_a) const = 0;
	virtual void pruned_clear (vban::write_transaction const &) = 0;
	virtual vban::store_iterator<vban::block_hash, std::nullptr_t> pruned_begin (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const = 0;
	virtual vban::store_iterator<vban::block_hash, std::nullptr_t> pruned_begin (vban::transaction const & transaction_a) const = 0;
	virtual vban::store_iterator<vban::block_hash, std::nullptr_t> pruned_end () const = 0;

	virtual void peer_put (vban::write_transaction const & transaction_a, vban::endpoint_key const & endpoint_a) = 0;
	virtual void peer_del (vban::write_transaction const & transaction_a, vban::endpoint_key const & endpoint_a) = 0;
	virtual bool peer_exists (vban::transaction const & transaction_a, vban::endpoint_key const & endpoint_a) const = 0;
	virtual size_t peer_count (vban::transaction const & transaction_a) const = 0;
	virtual void peer_clear (vban::write_transaction const & transaction_a) = 0;
	virtual vban::store_iterator<vban::endpoint_key, vban::no_value> peers_begin (vban::transaction const & transaction_a) const = 0;
	virtual vban::store_iterator<vban::endpoint_key, vban::no_value> peers_end () const = 0;

	virtual void confirmation_height_put (vban::write_transaction const & transaction_a, vban::account const & account_a, vban::confirmation_height_info const & confirmation_height_info_a) = 0;
	virtual bool confirmation_height_get (vban::transaction const & transaction_a, vban::account const & account_a, vban::confirmation_height_info & confirmation_height_info_a) = 0;
	virtual bool confirmation_height_exists (vban::transaction const & transaction_a, vban::account const & account_a) const = 0;
	virtual void confirmation_height_del (vban::write_transaction const & transaction_a, vban::account const & account_a) = 0;
	virtual uint64_t confirmation_height_count (vban::transaction const & transaction_a) = 0;
	virtual void confirmation_height_clear (vban::write_transaction const &, vban::account const &) = 0;
	virtual void confirmation_height_clear (vban::write_transaction const &) = 0;
	virtual vban::store_iterator<vban::account, vban::confirmation_height_info> confirmation_height_begin (vban::transaction const & transaction_a, vban::account const & account_a) const = 0;
	virtual vban::store_iterator<vban::account, vban::confirmation_height_info> confirmation_height_begin (vban::transaction const & transaction_a) const = 0;
	virtual vban::store_iterator<vban::account, vban::confirmation_height_info> confirmation_height_end () const = 0;

	virtual void accounts_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::account, vban::account_info>, vban::store_iterator<vban::account, vban::account_info>)> const &) const = 0;
	virtual void confirmation_height_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::account, vban::confirmation_height_info>, vban::store_iterator<vban::account, vban::confirmation_height_info>)> const &) const = 0;
	virtual void pending_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::pending_key, vban::pending_info>, vban::store_iterator<vban::pending_key, vban::pending_info>)> const & action_a) const = 0;
	virtual void unchecked_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::unchecked_key, vban::unchecked_info>, vban::store_iterator<vban::unchecked_key, vban::unchecked_info>)> const & action_a) const = 0;
	virtual void pruned_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::block_hash, std::nullptr_t>, vban::store_iterator<vban::block_hash, std::nullptr_t>)> const & action_a) const = 0;
	virtual void blocks_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::block_hash, block_w_sideband>, vban::store_iterator<vban::block_hash, block_w_sideband>)> const & action_a) const = 0;
	virtual void frontiers_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::block_hash, vban::account>, vban::store_iterator<vban::block_hash, vban::account>)> const & action_a) const = 0;
	virtual void final_vote_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::qualified_root, vban::block_hash>, vban::store_iterator<vban::qualified_root, vban::block_hash>)> const & action_a) const = 0;

	virtual uint64_t block_account_height (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const = 0;

	virtual bool final_vote_put (vban::write_transaction const & transaction_a, vban::qualified_root const & root_a, vban::block_hash const & hash_a) = 0;
	virtual std::vector<vban::block_hash> final_vote_get (vban::transaction const & transaction_a, vban::root const & root_a) = 0;
	virtual void final_vote_del (vban::write_transaction const & transaction_a, vban::root const & root_a) = 0;
	virtual size_t final_vote_count (vban::transaction const & transaction_a) const = 0;
	virtual void final_vote_clear (vban::write_transaction const &, vban::root const &) = 0;
	virtual void final_vote_clear (vban::write_transaction const &) = 0;
	virtual vban::store_iterator<vban::qualified_root, vban::block_hash> final_vote_begin (vban::transaction const & transaction_a, vban::qualified_root const & root_a) const = 0;
	virtual vban::store_iterator<vban::qualified_root, vban::block_hash> final_vote_begin (vban::transaction const & transaction_a) const = 0;
	virtual vban::store_iterator<vban::qualified_root, vban::block_hash> final_vote_end () const = 0;

	virtual unsigned max_block_write_batch_num () const = 0;

	virtual bool copy_db (boost::filesystem::path const & destination) = 0;
	virtual void rebuild_db (vban::write_transaction const & transaction_a) = 0;

	/** Not applicable to all sub-classes */
	virtual void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds){};
	virtual void serialize_memory_stats (boost::property_tree::ptree &) = 0;

	virtual bool init_error () const = 0;

	/** Start read-write transaction */
	virtual vban::write_transaction tx_begin_write (std::vector<vban::tables> const & tables_to_lock = {}, std::vector<vban::tables> const & tables_no_lock = {}) = 0;

	/** Start read-only transaction */
	virtual vban::read_transaction tx_begin_read () const = 0;

	virtual std::string vendor_get () const = 0;
};

std::unique_ptr<vban::block_store> make_store (vban::logger_mt & logger, boost::filesystem::path const & path, bool open_read_only = false, bool add_db_postfix = false, vban::rocksdb_config const & rocksdb_config = vban::rocksdb_config{}, vban::txn_tracking_config const & txn_tracking_config_a = vban::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), vban::lmdb_config const & lmdb_config_a = vban::lmdb_config{}, bool backup_before_upgrade = false);
}

namespace std
{
template <>
struct hash<::vban::tables>
{
	size_t operator() (::vban::tables const & table_a) const
	{
		return static_cast<size_t> (table_a);
	}
};
}
