#pragma once

#include <vban/lib/numbers.hpp>
#include <vban/node/election.hpp>
#include <vban/node/voting.hpp>
#include <vban/secure/common.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace vban
{
class node;
class block;
class block_sideband;
class election;
class election_scheduler;
class vote;
class transaction;
class confirmation_height_processor;
class stat;

class cementable_account final
{
public:
	cementable_account (vban::account const & account_a, size_t blocks_uncemented_a);
	vban::account account;
	uint64_t blocks_uncemented{ 0 };
};

class election_timepoint final
{
public:
	std::chrono::steady_clock::time_point time;
	vban::qualified_root root;
};

class inactive_cache_status final
{
public:
	bool bootstrap_started{ false };
	bool election_started{ false }; // Did item reach config threshold to start an impromptu election?
	bool confirmed{ false }; // Did item reach votes quorum? (minimum config value)
	vban::uint256_t tally{ 0 }; // Last votes tally for block

	bool operator!= (inactive_cache_status const other) const
	{
		return bootstrap_started != other.bootstrap_started || election_started != other.election_started || confirmed != other.confirmed || tally != other.tally;
	}
};

class inactive_cache_information final
{
public:
	inactive_cache_information () = default;
	inactive_cache_information (std::chrono::steady_clock::time_point arrival, vban::block_hash hash, vban::account initial_rep_a, uint64_t initial_timestamp_a, vban::inactive_cache_status status) :
		arrival (arrival),
		hash (hash),
		status (status)
	{
		voters.reserve (8);
		voters.emplace_back (initial_rep_a, initial_timestamp_a);
	}

	std::chrono::steady_clock::time_point arrival;
	vban::block_hash hash;
	vban::inactive_cache_status status;
	std::vector<std::pair<vban::account, uint64_t>> voters;
	bool needs_eval () const
	{
		return !status.bootstrap_started || !status.election_started || !status.confirmed;
	}
};

class expired_optimistic_election_info final
{
public:
	expired_optimistic_election_info (std::chrono::steady_clock::time_point, vban::account);

	std::chrono::steady_clock::time_point expired_time;
	vban::account account;
	bool election_started{ false };
};

class frontiers_confirmation_info
{
public:
	bool can_start_elections () const;

	size_t max_elections{ 0 };
	bool aggressive_mode{ false };
};

class election_insertion_result final
{
public:
	std::shared_ptr<vban::election> election;
	bool inserted{ false };
};

// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions final
{
	class conflict_info final
	{
	public:
		vban::qualified_root root;
		std::shared_ptr<vban::election> election;
		vban::epoch epoch;
		vban::uint256_t previous_balance;
	};

	friend class vban::election;

	// clang-format off
	class tag_account {};
	class tag_random_access {};
	class tag_root {};
	class tag_sequence {};
	class tag_uncemented {};
	class tag_arrival {};
	class tag_hash {};
	class tag_expired_time {};
	class tag_election_started {};
	// clang-format on

public:
	// clang-format off
	using ordered_roots = boost::multi_index_container<conflict_info,
	mi::indexed_by<
		mi::random_access<mi::tag<tag_random_access>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<conflict_info, vban::qualified_root, &conflict_info::root>>>>;
	// clang-format on
	ordered_roots roots;
	using roots_iterator = active_transactions::ordered_roots::index_iterator<tag_root>::type;

	explicit active_transactions (vban::node &, vban::confirmation_height_processor &);
	~active_transactions ();
	// Distinguishes replay votes, cannot be determined if the block is not in any election
	vban::vote_code vote (std::shared_ptr<vban::vote> const &);
	// Is the root of this block in the roots container
	bool active (vban::block const &);
	bool active (vban::qualified_root const &);
	std::shared_ptr<vban::election> election (vban::qualified_root const &) const;
	std::shared_ptr<vban::block> winner (vban::block_hash const &) const;
	// Returns false if the election was restarted
	void restart (vban::transaction const &, std::shared_ptr<vban::block> const &);
	// Returns a list of elections sorted by difficulty
	std::vector<std::shared_ptr<vban::election>> list_active (size_t = std::numeric_limits<size_t>::max ());
	void erase (vban::block const &);
	void erase_hash (vban::block_hash const &);
	void erase_oldest ();
	bool empty ();
	size_t size ();
	void stop ();
	bool publish (std::shared_ptr<vban::block> const &);
	boost::optional<vban::election_status_type> confirm_block (vban::transaction const &, std::shared_ptr<vban::block> const &);
	void block_cemented_callback (std::shared_ptr<vban::block> const &);
	void block_already_cemented_callback (vban::block_hash const &);

	int64_t vacancy () const;
	std::function<void ()> vacancy_update{ [] () {} };

	std::unordered_map<vban::block_hash, std::shared_ptr<vban::election>> blocks;
	std::deque<vban::election_status> list_recently_cemented ();
	std::deque<vban::election_status> recently_cemented;

	void add_recently_cemented (vban::election_status const &);
	void add_recently_confirmed (vban::qualified_root const &, vban::block_hash const &);
	void erase_recently_confirmed (vban::block_hash const &);
	void add_inactive_votes_cache (vban::unique_lock<vban::mutex> &, vban::block_hash const &, vban::account const &, uint64_t const);
	// Inserts an election if conditions are met
	void trigger_inactive_votes_cache_election (std::shared_ptr<vban::block> const &);
	vban::inactive_cache_information find_inactive_votes_cache (vban::block_hash const &);
	void erase_inactive_votes_cache (vban::block_hash const &);
	vban::election_scheduler & scheduler;
	vban::confirmation_height_processor & confirmation_height_processor;
	vban::node & node;
	mutable vban::mutex mutex{ mutex_identifier (mutexes::active) };
	size_t priority_cementable_frontiers_size ();
	size_t priority_wallet_cementable_frontiers_size ();
	size_t inactive_votes_cache_size ();
	size_t election_winner_details_size ();
	void add_election_winner_details (vban::block_hash const &, std::shared_ptr<vban::election> const &);
	void remove_election_winner_details (vban::block_hash const &);

	vban::vote_generator generator;
	vban::vote_generator final_generator;

#ifdef MEMORY_POOL_DISABLED
	using allocator = std::allocator<vban::inactive_cache_information>;
#else
	using allocator = boost::fast_pool_allocator<vban::inactive_cache_information>;
#endif

	// clang-format off
	using ordered_cache = boost::multi_index_container<vban::inactive_cache_information,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_arrival>,
			mi::member<vban::inactive_cache_information, std::chrono::steady_clock::time_point, &vban::inactive_cache_information::arrival>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<vban::inactive_cache_information, vban::block_hash, &vban::inactive_cache_information::hash>>>, allocator>;
	// clang-format on

private:
	vban::mutex election_winner_details_mutex{ mutex_identifier (mutexes::election_winner_details) };

	std::unordered_map<vban::block_hash, std::shared_ptr<vban::election>> election_winner_details;

	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	vban::election_insertion_result insert_impl (vban::unique_lock<vban::mutex> &, std::shared_ptr<vban::block> const&, boost::optional<vban::uint256_t> const & = boost::none, vban::election_behavior = vban::election_behavior::normal, std::function<void(std::shared_ptr<vban::block>const&)> const & = nullptr);
	// clang-format on
	void request_loop ();
	void request_confirm (vban::unique_lock<vban::mutex> &);
	void erase (vban::qualified_root const &);
	// Erase all blocks from active and, if not confirmed, clear digests from network filters
	void cleanup_election (vban::unique_lock<vban::mutex> & lock_a, vban::election const &);
	// Returns a list of elections sorted by difficulty, mutex must be locked
	std::vector<std::shared_ptr<vban::election>> list_active_impl (size_t) const;

	vban::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };

	// Maximum time an election can be kept active if it is extending the container
	std::chrono::seconds const election_time_to_live;

	static size_t constexpr recently_confirmed_size{ 65536 };
	using recent_confirmation = std::pair<vban::qualified_root, vban::block_hash>;
	// clang-format off
	boost::multi_index_container<recent_confirmation,
	mi::indexed_by<
		mi::sequenced<mi::tag<tag_sequence>>,
		mi::hashed_unique<mi::tag<tag_root>,
			mi::member<recent_confirmation, vban::qualified_root, &recent_confirmation::first>>,
		mi::hashed_unique<mi::tag<tag_hash>,
			mi::member<recent_confirmation, vban::block_hash, &recent_confirmation::second>>>>
	recently_confirmed;
	using prioritize_num_uncemented = boost::multi_index_container<vban::cementable_account,
	mi::indexed_by<
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<vban::cementable_account, vban::account, &vban::cementable_account::account>>,
		mi::ordered_non_unique<mi::tag<tag_uncemented>,
			mi::member<vban::cementable_account, uint64_t, &vban::cementable_account::blocks_uncemented>,
			std::greater<uint64_t>>>>;

	boost::multi_index_container<vban::expired_optimistic_election_info,
	mi::indexed_by<
		mi::ordered_non_unique<mi::tag<tag_expired_time>,
			mi::member<expired_optimistic_election_info, std::chrono::steady_clock::time_point, &expired_optimistic_election_info::expired_time>>,
		mi::hashed_unique<mi::tag<tag_account>,
			mi::member<expired_optimistic_election_info, vban::account, &expired_optimistic_election_info::account>>,
		mi::ordered_non_unique<mi::tag<tag_election_started>,
			mi::member<expired_optimistic_election_info, bool, &expired_optimistic_election_info::election_started>, std::greater<bool>>>>
	expired_optimistic_election_infos;
	// clang-format on
	std::atomic<uint64_t> expired_optimistic_election_infos_size{ 0 };

	// Frontiers confirmation
	vban::frontiers_confirmation_info get_frontiers_confirmation_info ();
	void confirm_prioritized_frontiers (vban::transaction const &, uint64_t, uint64_t &);
	void confirm_expired_frontiers_pessimistically (vban::transaction const &, uint64_t, uint64_t &);
	void frontiers_confirmation (vban::unique_lock<vban::mutex> &);
	bool insert_election_from_frontiers_confirmation (std::shared_ptr<vban::block> const &, vban::account const &, vban::uint256_t, vban::election_behavior);
	vban::account next_frontier_account{ 0 };
	std::chrono::steady_clock::time_point next_frontier_check{ std::chrono::steady_clock::now () };
	constexpr static size_t max_active_elections_frontier_insertion{ 1000 };
	prioritize_num_uncemented priority_wallet_cementable_frontiers;
	prioritize_num_uncemented priority_cementable_frontiers;
	std::unordered_set<vban::wallet_id> wallet_ids_already_iterated;
	std::unordered_map<vban::wallet_id, vban::account> next_wallet_id_accounts;
	bool skip_wallets{ false };
	std::atomic<unsigned> optimistic_elections_count{ 0 };
	void prioritize_frontiers_for_confirmation (vban::transaction const &, std::chrono::milliseconds, std::chrono::milliseconds);
	bool prioritize_account_for_confirmation (prioritize_num_uncemented &, size_t &, vban::account const &, vban::account_info const &, uint64_t);
	unsigned max_optimistic ();
	void set_next_frontier_check (bool);
	void add_expired_optimistic_election (vban::election const &);
	bool should_do_frontiers_confirmation () const;
	static size_t constexpr max_priority_cementable_frontiers{ 100000 };
	static size_t constexpr confirmed_frontiers_max_pending_size{ 10000 };
	static std::chrono::minutes constexpr expired_optimistic_election_info_cutoff{ 30 };
	ordered_cache inactive_votes_cache;
	vban::inactive_cache_status inactive_votes_bootstrap_check (vban::unique_lock<vban::mutex> &, std::vector<std::pair<vban::account, uint64_t>> const &, vban::block_hash const &, vban::inactive_cache_status const &);
	vban::inactive_cache_status inactive_votes_bootstrap_check (vban::unique_lock<vban::mutex> &, vban::account const &, vban::block_hash const &, vban::inactive_cache_status const &);
	vban::inactive_cache_status inactive_votes_bootstrap_check_impl (vban::unique_lock<vban::mutex> &, vban::uint256_t const &, size_t, vban::block_hash const &, vban::inactive_cache_status const &);
	vban::inactive_cache_information find_inactive_votes_cache_impl (vban::block_hash const &);
	boost::thread thread;

	friend class election;
	friend class election_scheduler;
	friend std::unique_ptr<container_info_component> collect_container_info (active_transactions &, const std::string &);

	friend class active_transactions_vote_replays_Test;
	friend class frontiers_confirmation_prioritize_frontiers_Test;
	friend class frontiers_confirmation_prioritize_frontiers_max_optimistic_elections_Test;
	friend class confirmation_height_prioritize_frontiers_overwrite_Test;
	friend class active_transactions_confirmation_consistency_Test;
	friend class node_deferred_dependent_elections_Test;
	friend class active_transactions_pessimistic_elections_Test;
	friend class frontiers_confirmation_expired_optimistic_elections_removal_Test;
};

bool purge_singleton_inactive_votes_cache_pool_memory ();
std::unique_ptr<container_info_component> collect_container_info (active_transactions & active_transactions, std::string const & name);
}
