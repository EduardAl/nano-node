#pragma once

#include <vban/lib/lmdbconfig.hpp>
#include <vban/lib/locks.hpp>
#include <vban/lib/work.hpp>
#include <vban/node/lmdb/lmdb.hpp>
#include <vban/node/lmdb/wallet_value.hpp>
#include <vban/node/openclwork.hpp>
#include <vban/secure/blockstore.hpp>
#include <vban/secure/common.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_set>
namespace vban
{
class node;
class node_config;
class wallets;
// The fan spreads a key out over the heap to decrease the likelihood of it being recovered by memory inspection
class fan final
{
public:
	fan (vban::raw_key const &, size_t);
	void value (vban::raw_key &);
	void value_set (vban::raw_key const &);
	std::vector<std::unique_ptr<vban::raw_key>> values;

private:
	vban::mutex mutex;
	void value_get (vban::raw_key &);
};
class kdf final
{
public:
	void phs (vban::raw_key &, std::string const &, vban::uint256_union const &);
	vban::mutex mutex;
};
enum class key_type
{
	not_a_type,
	unknown,
	adhoc,
	deterministic
};
class wallet_store final
{
public:
	wallet_store (bool &, vban::kdf &, vban::transaction &, vban::account, unsigned, std::string const &);
	wallet_store (bool &, vban::kdf &, vban::transaction &, vban::account, unsigned, std::string const &, std::string const &);
	std::vector<vban::account> accounts (vban::transaction const &);
	void initialize (vban::transaction const &, bool &, std::string const &);
	vban::uint256_union check (vban::transaction const &);
	bool rekey (vban::transaction const &, std::string const &);
	bool valid_password (vban::transaction const &);
	bool valid_public_key (vban::public_key const &);
	bool attempt_password (vban::transaction const &, std::string const &);
	void wallet_key (vban::raw_key &, vban::transaction const &);
	void seed (vban::raw_key &, vban::transaction const &);
	void seed_set (vban::transaction const &, vban::raw_key const &);
	vban::key_type key_type (vban::wallet_value const &);
	vban::public_key deterministic_insert (vban::transaction const &);
	vban::public_key deterministic_insert (vban::transaction const &, uint32_t const);
	vban::raw_key deterministic_key (vban::transaction const &, uint32_t);
	uint32_t deterministic_index_get (vban::transaction const &);
	void deterministic_index_set (vban::transaction const &, uint32_t);
	void deterministic_clear (vban::transaction const &);
	vban::uint256_union salt (vban::transaction const &);
	bool is_representative (vban::transaction const &);
	vban::account representative (vban::transaction const &);
	void representative_set (vban::transaction const &, vban::account const &);
	vban::public_key insert_adhoc (vban::transaction const &, vban::raw_key const &);
	bool insert_watch (vban::transaction const &, vban::account const &);
	void erase (vban::transaction const &, vban::account const &);
	vban::wallet_value entry_get_raw (vban::transaction const &, vban::account const &);
	void entry_put_raw (vban::transaction const &, vban::account const &, vban::wallet_value const &);
	bool fetch (vban::transaction const &, vban::account const &, vban::raw_key &);
	bool exists (vban::transaction const &, vban::account const &);
	void destroy (vban::transaction const &);
	vban::store_iterator<vban::account, vban::wallet_value> find (vban::transaction const &, vban::account const &);
	vban::store_iterator<vban::account, vban::wallet_value> begin (vban::transaction const &, vban::account const &);
	vban::store_iterator<vban::account, vban::wallet_value> begin (vban::transaction const &);
	vban::store_iterator<vban::account, vban::wallet_value> end ();
	void derive_key (vban::raw_key &, vban::transaction const &, std::string const &);
	void serialize_json (vban::transaction const &, std::string &);
	void write_backup (vban::transaction const &, boost::filesystem::path const &);
	bool move (vban::transaction const &, vban::wallet_store &, std::vector<vban::public_key> const &);
	bool import (vban::transaction const &, vban::wallet_store &);
	bool work_get (vban::transaction const &, vban::public_key const &, uint64_t &);
	void work_put (vban::transaction const &, vban::public_key const &, uint64_t);
	unsigned version (vban::transaction const &);
	void version_put (vban::transaction const &, unsigned);
	vban::fan password;
	vban::fan wallet_key_mem;
	static unsigned const version_1 = 1;
	static unsigned const version_2 = 2;
	static unsigned const version_3 = 3;
	static unsigned const version_4 = 4;
	static unsigned constexpr version_current = version_4;
	static vban::account const version_special;
	static vban::account const wallet_key_special;
	static vban::account const salt_special;
	static vban::account const check_special;
	static vban::account const representative_special;
	static vban::account const seed_special;
	static vban::account const deterministic_index_special;
	static size_t const check_iv_index;
	static size_t const seed_iv_index;
	static int const special_count;
	vban::kdf & kdf;
	std::atomic<MDB_dbi> handle{ 0 };
	std::recursive_mutex mutex;

private:
	MDB_txn * tx (vban::transaction const &) const;
};
// A wallet is a set of account keys encrypted by a common encryption key
class wallet final : public std::enable_shared_from_this<vban::wallet>
{
public:
	std::shared_ptr<vban::block> change_action (vban::account const &, vban::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<vban::block> receive_action (vban::block_hash const &, vban::account const &, vban::uint128_union const &, vban::account const &, uint64_t = 0, bool = true);
	std::shared_ptr<vban::block> send_action (vban::account const &, vban::account const &, vban::uint256_t const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	bool action_complete (std::shared_ptr<vban::block> const &, vban::account const &, bool const, vban::block_details const &);
	wallet (bool &, vban::transaction &, vban::wallets &, std::string const &);
	wallet (bool &, vban::transaction &, vban::wallets &, std::string const &, std::string const &);
	void enter_initial_password ();
	bool enter_password (vban::transaction const &, std::string const &);
	vban::public_key insert_adhoc (vban::raw_key const &, bool = true);
	bool insert_watch (vban::transaction const &, vban::public_key const &);
	vban::public_key deterministic_insert (vban::transaction const &, bool = true);
	vban::public_key deterministic_insert (uint32_t, bool = true);
	vban::public_key deterministic_insert (bool = true);
	bool exists (vban::public_key const &);
	bool import (std::string const &, std::string const &);
	void serialize (std::string &);
	bool change_sync (vban::account const &, vban::account const &);
	void change_async (vban::account const &, vban::account const &, std::function<void (std::shared_ptr<vban::block> const &)> const &, uint64_t = 0, bool = true);
	bool receive_sync (std::shared_ptr<vban::block> const &, vban::account const &, vban::uint256_t const &);
	void receive_async (vban::block_hash const &, vban::account const &, vban::uint256_t const &, vban::account const &, std::function<void (std::shared_ptr<vban::block> const &)> const &, uint64_t = 0, bool = true);
	vban::block_hash send_sync (vban::account const &, vban::account const &, vban::uint256_t const &);
	void send_async (vban::account const &, vban::account const &, vban::uint256_t const &, std::function<void (std::shared_ptr<vban::block> const &)> const &, uint64_t = 0, bool = true, boost::optional<std::string> = {});
	void work_cache_blocking (vban::account const &, vban::root const &);
	void work_update (vban::transaction const &, vban::account const &, vban::root const &, uint64_t);
	// Schedule work generation after a few seconds
	void work_ensure (vban::account const &, vban::root const &);
	bool search_pending (vban::transaction const &);
	void init_free_accounts (vban::transaction const &);
	uint32_t deterministic_check (vban::transaction const & transaction_a, uint32_t index);
	/** Changes the wallet seed and returns the first account */
	vban::public_key change_seed (vban::transaction const & transaction_a, vban::raw_key const & prv_a, uint32_t count = 0);
	void deterministic_restore (vban::transaction const & transaction_a);
	bool live ();
	vban::network_params network_params;
	std::unordered_set<vban::account> free_accounts;
	std::function<void (bool, bool)> lock_observer;
	vban::wallet_store store;
	vban::wallets & wallets;
	vban::mutex representatives_mutex;
	std::unordered_set<vban::account> representatives;
};

class wallet_representatives
{
public:
	uint64_t voting{ 0 }; // Number of representatives with at least the configured minimum voting weight
	uint64_t half_principal{ 0 }; // Number of representatives with at least 50% of principal representative requirements
	std::unordered_set<vban::account> accounts; // Representatives with at least the configured minimum voting weight
	bool have_half_rep () const
	{
		return half_principal > 0;
	}
	bool exists (vban::account const & rep_a) const
	{
		return accounts.count (rep_a) > 0;
	}
	void clear ()
	{
		voting = 0;
		half_principal = 0;
		accounts.clear ();
	}
};

/**
 * The wallets set is all the wallets a node controls.
 * A node may contain multiple wallets independently encrypted and operated.
 */
class wallets final
{
public:
	wallets (bool, vban::node &);
	~wallets ();
	std::shared_ptr<vban::wallet> open (vban::wallet_id const &);
	std::shared_ptr<vban::wallet> create (vban::wallet_id const &);
	bool search_pending (vban::wallet_id const &);
	void search_pending_all ();
	void destroy (vban::wallet_id const &);
	void reload ();
	void do_wallet_actions ();
	void queue_wallet_action (vban::uint256_t const &, std::shared_ptr<vban::wallet> const &, std::function<void (vban::wallet &)>);
	void foreach_representative (std::function<void (vban::public_key const &, vban::raw_key const &)> const &);
	bool exists (vban::transaction const &, vban::account const &);
	void stop ();
	void clear_send_ids (vban::transaction const &);
	vban::wallet_representatives reps () const;
	bool check_rep (vban::account const &, vban::uint256_t const &, const bool = true);
	void compute_reps ();
	void ongoing_compute_reps ();
	void split_if_needed (vban::transaction &, vban::block_store &);
	void move_table (std::string const &, MDB_txn *, MDB_txn *);
	std::unordered_map<vban::wallet_id, std::shared_ptr<vban::wallet>> get_wallets ();
	vban::network_params network_params;
	std::function<void (bool)> observer;
	std::unordered_map<vban::wallet_id, std::shared_ptr<vban::wallet>> items;
	std::multimap<vban::uint256_t, std::pair<std::shared_ptr<vban::wallet>, std::function<void (vban::wallet &)>>, std::greater<vban::uint256_t>> actions;
	vban::locked<std::unordered_map<vban::account, vban::root>> delayed_work;
	vban::mutex mutex;
	vban::mutex action_mutex;
	vban::condition_variable condition;
	vban::kdf kdf;
	MDB_dbi handle;
	MDB_dbi send_action_ids;
	vban::node & node;
	vban::mdb_env & env;
	std::atomic<bool> stopped;
	std::thread thread;
	static vban::uint256_t const generate_priority;
	static vban::uint256_t const high_priority;
	/** Start read-write transaction */
	vban::write_transaction tx_begin_write ();

	/** Start read-only transaction */
	vban::read_transaction tx_begin_read ();

private:
	mutable vban::mutex reps_cache_mutex;
	vban::wallet_representatives representatives;
};

std::unique_ptr<container_info_component> collect_container_info (wallets & wallets, std::string const & name);

class wallets_store
{
public:
	virtual ~wallets_store () = default;
	virtual bool init_error () const = 0;
};
class mdb_wallets_store final : public wallets_store
{
public:
	mdb_wallets_store (boost::filesystem::path const &, vban::lmdb_config const & lmdb_config_a = vban::lmdb_config{});
	vban::mdb_env environment;
	bool init_error () const override;
	bool error{ false };
};
}
