#pragma once

#include <vban/lib/diagnosticsconfig.hpp>
#include <vban/lib/lmdbconfig.hpp>
#include <vban/lib/logger_mt.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/node/lmdb/lmdb_env.hpp>
#include <vban/node/lmdb/lmdb_iterator.hpp>
#include <vban/node/lmdb/lmdb_txn.hpp>
#include <vban/secure/blockstore_partial.hpp>
#include <vban/secure/common.hpp>
#include <vban/secure/versioning.hpp>

#include <boost/optional.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace vban
{
using mdb_val = db_val<MDB_val>;

class logging_mt;
/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store_partial<MDB_val, mdb_store>
{
public:
	using block_store_partial::block_exists;
	using block_store_partial::unchecked_put;

	mdb_store (vban::logger_mt &, boost::filesystem::path const &, vban::txn_tracking_config const & txn_tracking_config_a = vban::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), vban::lmdb_config const & lmdb_config_a = vban::lmdb_config{}, bool backup_before_upgrade = false);
	vban::write_transaction tx_begin_write (std::vector<vban::tables> const & tables_requiring_lock = {}, std::vector<vban::tables> const & tables_no_lock = {}) override;
	vban::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	void version_put (vban::write_transaction const &, int) override;

	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	static void create_backup_file (vban::mdb_env &, boost::filesystem::path const &, vban::logger_mt &);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	unsigned max_block_write_batch_num () const override;

private:
	vban::logger_mt & logger;
	bool error{ false };

public:
	vban::mdb_env env;

	/**
	 * Maps head block to owning account
	 * vban::block_hash -> vban::account
	 */
	MDB_dbi frontiers{ 0 };

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * vban::account -> vban::block_hash, vban::block_hash, vban::block_hash, vban::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count. (Removed)
	 * vban::account -> vban::block_hash, vban::block_hash, vban::block_hash, vban::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp, block count and epoch
	 * vban::account -> vban::block_hash, vban::block_hash, vban::block_hash, vban::amount, uint64_t, uint64_t, vban::epoch
	 */
	MDB_dbi accounts{ 0 };

	/**
	 * Maps block hash to send block. (Removed)
	 * vban::block_hash -> vban::send_block
	 */
	MDB_dbi send_blocks{ 0 };

	/**
	 * Maps block hash to receive block. (Removed)
	 * vban::block_hash -> vban::receive_block
	 */
	MDB_dbi receive_blocks{ 0 };

	/**
	 * Maps block hash to open block. (Removed)
	 * vban::block_hash -> vban::open_block
	 */
	MDB_dbi open_blocks{ 0 };

	/**
	 * Maps block hash to change block. (Removed)
	 * vban::block_hash -> vban::change_block
	 */
	MDB_dbi change_blocks{ 0 };

	/**
	 * Maps block hash to v0 state block. (Removed)
	 * vban::block_hash -> vban::state_block
	 */
	MDB_dbi state_blocks_v0{ 0 };

	/**
	 * Maps block hash to v1 state block. (Removed)
	 * vban::block_hash -> vban::state_block
	 */
	MDB_dbi state_blocks_v1{ 0 };

	/**
	 * Maps block hash to state block. (Removed)
	 * vban::block_hash -> vban::state_block
	 */
	MDB_dbi state_blocks{ 0 };

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount). (Removed)
	 * vban::account, vban::block_hash -> vban::account, vban::amount
	 */
	MDB_dbi pending_v0{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount). (Removed)
	 * vban::account, vban::block_hash -> vban::account, vban::amount
	 */
	MDB_dbi pending_v1{ 0 };

	/**
	 * Maps (destination account, pending block) to (source account, amount, version). (Removed)
	 * vban::account, vban::block_hash -> vban::account, vban::amount, vban::epoch
	 */
	MDB_dbi pending{ 0 };

	/**
	 * Representative weights. (Removed)
	 * vban::account -> vban::uint256_t
	 */
	MDB_dbi representation{ 0 };

	/**
	 * Unchecked bootstrap blocks info.
	 * vban::block_hash -> vban::unchecked_info
	 */
	MDB_dbi unchecked{ 0 };

	/**
	 * Samples of online vote weight
	 * uint64_t -> vban::amount
	 */
	MDB_dbi online_weight{ 0 };

	/**
	 * Meta information about block store, such as versions.
	 * vban::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta{ 0 };

	/**
	 * Pruned blocks hashes
	 * vban::block_hash -> none
	 */
	MDB_dbi pruned{ 0 };

	/*
	 * Endpoints for peers
	 * vban::endpoint_key -> no_value
	*/
	MDB_dbi peers{ 0 };

	/*
	 * Confirmation height of an account, and the hash for the block at that height
	 * vban::account -> uint64_t, vban::block_hash
	 */
	MDB_dbi confirmation_height{ 0 };

	/*
	 * Contains block_sideband and block for all block types (legacy send/change/open/receive & state blocks)
	 * vban::block_hash -> vban::block_sideband, vban::block
	 */
	MDB_dbi blocks{ 0 };

	/**
	 * Maps root to block hash for generated final votes.
	 * vban::qualified_root -> vban::block_hash
	 */
	MDB_dbi final_votes{ 0 };

	bool exists (vban::transaction const & transaction_a, tables table_a, vban::mdb_val const & key_a) const;
	std::vector<vban::unchecked_info> unchecked_get (vban::transaction const & transaction_a, vban::block_hash const & hash_a) override;

	int get (vban::transaction const & transaction_a, tables table_a, vban::mdb_val const & key_a, vban::mdb_val & value_a) const;
	int put (vban::write_transaction const & transaction_a, tables table_a, vban::mdb_val const & key_a, const vban::mdb_val & value_a) const;
	int del (vban::write_transaction const & transaction_a, tables table_a, vban::mdb_val const & key_a) const;

	bool copy_db (boost::filesystem::path const & destination_file) override;
	void rebuild_db (vban::write_transaction const & transaction_a) override;

	template <typename Key, typename Value>
	vban::store_iterator<Key, Value> make_iterator (vban::transaction const & transaction_a, tables table_a, bool const direction_asc) const
	{
		return vban::store_iterator<Key, Value> (std::make_unique<vban::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), vban::mdb_val{}, direction_asc));
	}

	template <typename Key, typename Value>
	vban::store_iterator<Key, Value> make_iterator (vban::transaction const & transaction_a, tables table_a, vban::mdb_val const & key) const
	{
		return vban::store_iterator<Key, Value> (std::make_unique<vban::mdb_iterator<Key, Value>> (transaction_a, table_to_dbi (table_a), key));
	}

	bool init_error () const override;

	uint64_t count (vban::transaction const &, MDB_dbi) const;
	std::string error_string (int status) const override;

	// These are only use in the upgrade process.
	std::shared_ptr<vban::block> block_get_v14 (vban::transaction const & transaction_a, vban::block_hash const & hash_a, vban::block_sideband_v14 * sideband_a = nullptr, bool * is_state_v1 = nullptr) const;
	size_t block_successor_offset_v14 (vban::transaction const & transaction_a, size_t entry_size_a, vban::block_type type_a) const;
	vban::block_hash block_successor_v14 (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const;
	vban::mdb_val block_raw_get_v14 (vban::transaction const & transaction_a, vban::block_hash const & hash_a, vban::block_type & type_a, bool * is_state_v1 = nullptr) const;
	boost::optional<vban::mdb_val> block_raw_get_by_type_v14 (vban::transaction const & transaction_a, vban::block_hash const & hash_a, vban::block_type & type_a, bool * is_state_v1) const;

private:
	bool do_upgrades (vban::write_transaction &, bool &);
	void upgrade_v14_to_v15 (vban::write_transaction &);
	void upgrade_v15_to_v16 (vban::write_transaction const &);
	void upgrade_v16_to_v17 (vban::write_transaction const &);
	void upgrade_v17_to_v18 (vban::write_transaction const &);
	void upgrade_v18_to_v19 (vban::write_transaction const &);
	void upgrade_v19_to_v20 (vban::write_transaction const &);
	void upgrade_v20_to_v21 (vban::write_transaction const &);

	std::shared_ptr<vban::block> block_get_v18 (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const;
	vban::mdb_val block_raw_get_v18 (vban::transaction const & transaction_a, vban::block_hash const & hash_a, vban::block_type & type_a) const;
	boost::optional<vban::mdb_val> block_raw_get_by_type_v18 (vban::transaction const & transaction_a, vban::block_hash const & hash_a, vban::block_type & type_a) const;
	vban::uint256_t block_balance_v18 (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const;

	void open_databases (bool &, vban::transaction const &, unsigned);

	int drop (vban::write_transaction const & transaction_a, tables table_a) override;
	int clear (vban::write_transaction const & transaction_a, MDB_dbi handle_a);

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;

	MDB_dbi table_to_dbi (tables table_a) const;

	mutable vban::mdb_txn_tracker mdb_txn_tracker;
	vban::mdb_txn_callbacks create_txn_callbacks () const;
	bool txn_tracking_enabled;

	uint64_t count (vban::transaction const & transaction_a, tables table_a) const override;

	bool vacuum_after_upgrade (boost::filesystem::path const & path_a, vban::lmdb_config const & lmdb_config_a);

	class upgrade_counters
	{
	public:
		upgrade_counters (uint64_t count_before_v0, uint64_t count_before_v1);
		bool are_equal () const;

		uint64_t before_v0;
		uint64_t before_v1;
		uint64_t after_v0{ 0 };
		uint64_t after_v1{ 0 };
	};
};

template <>
void * mdb_val::data () const;
template <>
size_t mdb_val::size () const;
template <>
mdb_val::db_val (size_t size_a, void * data_a);
template <>
void mdb_val::convert_buffer_to_value ();

extern template class block_store_partial<MDB_val, mdb_store>;
}
