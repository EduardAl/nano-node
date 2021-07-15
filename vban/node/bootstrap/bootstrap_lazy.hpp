#pragma once

#include <vban/node/bootstrap/bootstrap_attempt.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <queue>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace vban
{
class node;
class lazy_state_backlog_item final
{
public:
	vban::link link{ 0 };
	vban::uint256_t balance{ 0 };
	unsigned retry_limit{ 0 };
};
class bootstrap_attempt_lazy final : public bootstrap_attempt
{
public:
	explicit bootstrap_attempt_lazy (std::shared_ptr<vban::node> const & node_a, uint64_t incremental_id_a, std::string const & id_a = "");
	~bootstrap_attempt_lazy ();
	bool process_block (std::shared_ptr<vban::block> const &, vban::account const &, uint64_t, vban::bulk_pull::count_t, bool, unsigned) override;
	void run () override;
	bool lazy_start (vban::hash_or_account const &, bool confirmed = true) override;
	void lazy_add (vban::hash_or_account const &, unsigned);
	void lazy_add (vban::pull_info const &) override;
	void lazy_requeue (vban::block_hash const &, vban::block_hash const &, bool) override;
	bool lazy_finished ();
	bool lazy_has_expired () const override;
	uint32_t lazy_batch_size () override;
	void lazy_pull_flush (vban::unique_lock<vban::mutex> & lock_a);
	bool process_block_lazy (std::shared_ptr<vban::block> const &, vban::account const &, uint64_t, vban::bulk_pull::count_t, unsigned);
	void lazy_block_state (std::shared_ptr<vban::block> const &, unsigned);
	void lazy_block_state_backlog_check (std::shared_ptr<vban::block> const &, vban::block_hash const &);
	void lazy_backlog_cleanup ();
	void lazy_blocks_insert (vban::block_hash const &);
	void lazy_blocks_erase (vban::block_hash const &);
	bool lazy_blocks_processed (vban::block_hash const &);
	bool lazy_processed_or_exists (vban::block_hash const &) override;
	unsigned lazy_retry_limit_confirmed ();
	void get_information (boost::property_tree::ptree &) override;
	std::unordered_set<size_t> lazy_blocks;
	std::unordered_map<vban::block_hash, vban::lazy_state_backlog_item> lazy_state_backlog;
	std::unordered_set<vban::block_hash> lazy_undefined_links;
	std::unordered_map<vban::block_hash, vban::uint256_t> lazy_balances;
	std::unordered_set<vban::block_hash> lazy_keys;
	std::deque<std::pair<vban::hash_or_account, unsigned>> lazy_pulls;
	std::chrono::steady_clock::time_point lazy_start_time;
	std::atomic<size_t> lazy_blocks_count{ 0 };
	size_t peer_count{ 0 };
	/** The maximum number of records to be read in while iterating over long lazy containers */
	static uint64_t constexpr batch_read_size = 256;
};
class bootstrap_attempt_wallet final : public bootstrap_attempt
{
public:
	explicit bootstrap_attempt_wallet (std::shared_ptr<vban::node> const & node_a, uint64_t incremental_id_a, std::string id_a = "");
	~bootstrap_attempt_wallet ();
	void request_pending (vban::unique_lock<vban::mutex> &);
	void requeue_pending (vban::account const &) override;
	void run () override;
	void wallet_start (std::deque<vban::account> &) override;
	bool wallet_finished ();
	size_t wallet_size () override;
	void get_information (boost::property_tree::ptree &) override;
	std::deque<vban::account> wallet_accounts;
};
}
