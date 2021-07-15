#pragma once

#include <vban/lib/config.hpp>
#include <vban/lib/logger_mt.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/node/rocksdb/rocksdb_iterator.hpp>
#include <vban/secure/blockstore_partial.hpp>
#include <vban/secure/common.hpp>

#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/table.h>
#include <rocksdb/utilities/optimistic_transaction_db.h>
#include <rocksdb/utilities/transaction.h>

namespace vban
{
class logging_mt;
class rocksdb_config;

/**
 * rocksdb implementation of the block store
 */
class rocksdb_store : public block_store_partial<rocksdb::Slice, rocksdb_store>
{
public:
	rocksdb_store (vban::logger_mt &, boost::filesystem::path const &, vban::rocksdb_config const & = vban::rocksdb_config{}, bool open_read_only = false);
	vban::write_transaction tx_begin_write (std::vector<vban::tables> const & tables_requiring_lock = {}, std::vector<vban::tables> const & tables_no_lock = {}) override;
	vban::read_transaction tx_begin_read () const override;

	std::string vendor_get () const override;

	uint64_t count (vban::transaction const & transaction_a, tables table_a) const override;
	void version_put (vban::write_transaction const &, int) override;
	std::vector<vban::unchecked_info> unchecked_get (vban::transaction const & transaction_a, vban::block_hash const & hash_a) override;

	bool exists (vban::transaction const & transaction_a, tables table_a, vban::rocksdb_val const & key_a) const;
	int get (vban::transaction const & transaction_a, tables table_a, vban::rocksdb_val const & key_a, vban::rocksdb_val & value_a) const;
	int put (vban::write_transaction const & transaction_a, tables table_a, vban::rocksdb_val const & key_a, vban::rocksdb_val const & value_a);
	int del (vban::write_transaction const & transaction_a, tables table_a, vban::rocksdb_val const & key_a);

	void serialize_memory_stats (boost::property_tree::ptree &) override;

	bool copy_db (boost::filesystem::path const & destination) override;
	void rebuild_db (vban::write_transaction const & transaction_a) override;

	unsigned max_block_write_batch_num () const override;

	template <typename Key, typename Value>
	vban::store_iterator<Key, Value> make_iterator (vban::transaction const & transaction_a, tables table_a, bool const direction_asc) const
	{
		return vban::store_iterator<Key, Value> (std::make_unique<vban::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), nullptr, direction_asc));
	}

	template <typename Key, typename Value>
	vban::store_iterator<Key, Value> make_iterator (vban::transaction const & transaction_a, tables table_a, vban::rocksdb_val const & key) const
	{
		return vban::store_iterator<Key, Value> (std::make_unique<vban::rocksdb_iterator<Key, Value>> (db.get (), transaction_a, table_to_column_family (table_a), &key, true));
	}

	bool init_error () const override;

	std::string error_string (int status) const override;

private:
	bool error{ false };
	vban::logger_mt & logger;
	// Optimistic transactions are used in write mode
	rocksdb::OptimisticTransactionDB * optimistic_db = nullptr;
	std::unique_ptr<rocksdb::DB> db;
	std::vector<std::unique_ptr<rocksdb::ColumnFamilyHandle>> handles;
	std::shared_ptr<rocksdb::TableFactory> small_table_factory;
	std::unordered_map<vban::tables, vban::mutex> write_lock_mutexes;
	vban::rocksdb_config rocksdb_config;
	unsigned const max_block_write_batch_num_m;

	class tombstone_info
	{
	public:
		tombstone_info (uint64_t, uint64_t const);
		std::atomic<uint64_t> num_since_last_flush;
		uint64_t const max;
	};

	std::unordered_map<vban::tables, tombstone_info> tombstone_map;
	std::unordered_map<const char *, vban::tables> cf_name_table_map;

	rocksdb::Transaction * tx (vban::transaction const & transaction_a) const;
	std::vector<vban::tables> all_tables () const;

	bool not_found (int status) const override;
	bool success (int status) const override;
	int status_code_not_found () const override;
	int drop (vban::write_transaction const &, tables) override;

	rocksdb::ColumnFamilyHandle * table_to_column_family (tables table_a) const;
	int clear (rocksdb::ColumnFamilyHandle * column_family);

	void open (bool & error_a, boost::filesystem::path const & path_a, bool open_read_only_a);

	void construct_column_family_mutexes ();
	rocksdb::Options get_db_options ();
	rocksdb::ColumnFamilyOptions get_common_cf_options (std::shared_ptr<rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const;
	rocksdb::ColumnFamilyOptions get_active_cf_options (std::shared_ptr<rocksdb::TableFactory> const & table_factory_a, unsigned long long memtable_size_bytes_a) const;
	rocksdb::ColumnFamilyOptions get_small_cf_options (std::shared_ptr<rocksdb::TableFactory> const & table_factory_a) const;
	rocksdb::BlockBasedTableOptions get_active_table_options (size_t lru_size) const;
	rocksdb::BlockBasedTableOptions get_small_table_options () const;
	rocksdb::ColumnFamilyOptions get_cf_options (std::string const & cf_name_a) const;

	void on_flush (rocksdb::FlushJobInfo const &);
	void flush_table (vban::tables table_a);
	void flush_tombstones_check (vban::tables table_a);
	void generate_tombstone_map ();
	std::unordered_map<const char *, vban::tables> create_cf_name_table_map () const;

	std::vector<rocksdb::ColumnFamilyDescriptor> create_column_families ();
	unsigned long long base_memtable_size_bytes () const;
	unsigned long long blocks_memtable_size_bytes () const;

	constexpr static int base_memtable_size = 16;
	constexpr static int base_block_cache_size = 8;

	friend class rocksdb_block_store_tombstone_count_Test;
};

extern template class block_store_partial<rocksdb::Slice, rocksdb_store>;
}
