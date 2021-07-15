#pragma once

#include <vban/lib/config.hpp>
#include <vban/lib/stats.hpp>
#include <vban/lib/work.hpp>
#include <vban/node/active_transactions.hpp>
#include <vban/node/blockprocessor.hpp>
#include <vban/node/bootstrap/bootstrap.hpp>
#include <vban/node/bootstrap/bootstrap_attempt.hpp>
#include <vban/node/bootstrap/bootstrap_server.hpp>
#include <vban/node/confirmation_height_processor.hpp>
#include <vban/node/distributed_work_factory.hpp>
#include <vban/node/election.hpp>
#include <vban/node/election_scheduler.hpp>
#include <vban/node/gap_cache.hpp>
#include <vban/node/network.hpp>
#include <vban/node/node_observers.hpp>
#include <vban/node/nodeconfig.hpp>
#include <vban/node/online_reps.hpp>
#include <vban/node/portmapping.hpp>
#include <vban/node/repcrawler.hpp>
#include <vban/node/request_aggregator.hpp>
#include <vban/node/signatures.hpp>
#include <vban/node/telemetry.hpp>
#include <vban/node/vote_processor.hpp>
#include <vban/node/wallet.hpp>
#include <vban/node/write_database_queue.hpp>
#include <vban/secure/ledger.hpp>
#include <vban/secure/utility.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/latch.hpp>

#include <atomic>
#include <memory>
#include <vector>

namespace vban
{
namespace websocket
{
	class listener;
}
class node;
class telemetry;
class work_pool;
class block_arrival_info final
{
public:
	std::chrono::steady_clock::time_point arrival;
	vban::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival final
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (vban::block_hash const &);
	bool recent (vban::block_hash const &);
	// clang-format off
	class tag_sequence {};
	class tag_hash {};
	boost::multi_index_container<vban::block_arrival_info,
		boost::multi_index::indexed_by<
			boost::multi_index::sequenced<boost::multi_index::tag<tag_sequence>>,
			boost::multi_index::hashed_unique<boost::multi_index::tag<tag_hash>,
				boost::multi_index::member<vban::block_arrival_info, vban::block_hash, &vban::block_arrival_info::hash>>>>
	arrival;
	// clang-format on
	vban::mutex mutex{ mutex_identifier (mutexes::block_arrival) };
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};

std::unique_ptr<container_info_component> collect_container_info (block_arrival & block_arrival, std::string const & name);

std::unique_ptr<container_info_component> collect_container_info (rep_crawler & rep_crawler, std::string const & name);

class node final : public std::enable_shared_from_this<vban::node>
{
public:
	node (boost::asio::io_context &, uint16_t, boost::filesystem::path const &, vban::logging const &, vban::work_pool &, vban::node_flags = vban::node_flags (), unsigned seq = 0);
	node (boost::asio::io_context &, boost::filesystem::path const &, vban::node_config const &, vban::work_pool &, vban::node_flags = vban::node_flags (), unsigned seq = 0);
	~node ();
	template <typename T>
	void background (T action_a)
	{
		io_ctx.post (action_a);
	}
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<vban::node> shared ();
	int store_version ();
	void receive_confirmed (vban::transaction const & block_transaction_a, vban::block_hash const & hash_a, vban::account const & destination_a);
	void process_confirmed_data (vban::transaction const &, std::shared_ptr<vban::block> const &, vban::block_hash const &, vban::account &, vban::uint256_t &, bool &, vban::account &);
	void process_confirmed (vban::election_status const &, uint64_t = 0);
	void process_active (std::shared_ptr<vban::block> const &);
	vban::process_return process (vban::block &);
	vban::process_return process_local (std::shared_ptr<vban::block> const &);
	void process_local_async (std::shared_ptr<vban::block> const &);
	void keepalive_preconfigured (std::vector<std::string> const &);
	vban::block_hash latest (vban::account const &);
	vban::uint256_t balance (vban::account const &);
	std::shared_ptr<vban::block> block (vban::block_hash const &);
	std::pair<vban::uint256_t, vban::uint256_t> balance_pending (vban::account const &, bool only_confirmed);
	vban::uint256_t weight (vban::account const &);
	vban::block_hash rep_block (vban::account const &);
	vban::uint256_t minimum_principal_weight ();
	vban::uint256_t minimum_principal_weight (vban::uint256_t const &);
	void ongoing_rep_calculation ();
	void ongoing_bootstrap ();
	void ongoing_peer_store ();
	void ongoing_unchecked_cleanup ();
	void ongoing_backlog_population ();
	void backup_wallet ();
	void search_pending ();
	void bootstrap_wallet ();
	void unchecked_cleanup ();
	bool collect_ledger_pruning_targets (std::deque<vban::block_hash> &, vban::account &, uint64_t const, uint64_t const, uint64_t const);
	void ledger_pruning (uint64_t const, bool, bool);
	void ongoing_ledger_pruning ();
	int price (vban::uint256_t const &, int);
	// The default difficulty updates to base only when the first epoch_2 block is processed
	uint64_t default_difficulty (vban::work_version const) const;
	uint64_t default_receive_difficulty (vban::work_version const) const;
	uint64_t max_work_generate_difficulty (vban::work_version const) const;
	bool local_work_generation_enabled () const;
	bool work_generation_enabled () const;
	bool work_generation_enabled (std::vector<std::pair<std::string, uint16_t>> const &) const;
	boost::optional<uint64_t> work_generate_blocking (vban::block &, uint64_t);
	boost::optional<uint64_t> work_generate_blocking (vban::work_version const, vban::root const &, uint64_t, boost::optional<vban::account> const & = boost::none);
	void work_generate (vban::work_version const, vban::root const &, uint64_t, std::function<void (boost::optional<uint64_t>)>, boost::optional<vban::account> const & = boost::none, bool const = false);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<vban::block> const &);
	bool block_confirmed (vban::block_hash const &);
	bool block_confirmed_or_being_confirmed (vban::transaction const &, vban::block_hash const &);
	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string> const &, std::shared_ptr<std::string> const &, std::shared_ptr<boost::asio::ip::tcp::resolver> const &);
	void ongoing_online_weight_calculation ();
	void ongoing_online_weight_calculation_queue ();
	bool online () const;
	bool init_error () const;
	bool epoch_upgrader (vban::raw_key const &, vban::epoch, uint64_t, uint64_t);
	void set_bandwidth_params (size_t limit, double ratio);
	std::pair<uint64_t, decltype (vban::ledger::bootstrap_weights)> get_bootstrap_weights () const;
	void populate_backlog ();
	vban::write_database_queue write_database_queue;
	boost::asio::io_context & io_ctx;
	boost::latch node_initialized_latch;
	vban::network_params network_params;
	vban::node_config config;
	vban::stat stats;
	vban::thread_pool workers;
	std::shared_ptr<vban::websocket::listener> websocket_server;
	vban::node_flags flags;
	vban::work_pool & work;
	vban::distributed_work_factory distributed_work;
	vban::logger_mt logger;
	std::unique_ptr<vban::block_store> store_impl;
	vban::block_store & store;
	std::unique_ptr<vban::wallets_store> wallets_store_impl;
	vban::wallets_store & wallets_store;
	vban::gap_cache gap_cache;
	vban::ledger ledger;
	vban::signature_checker checker;
	vban::network network;
	std::shared_ptr<vban::telemetry> telemetry;
	vban::bootstrap_initiator bootstrap_initiator;
	vban::bootstrap_listener bootstrap;
	boost::filesystem::path application_path;
	vban::node_observers observers;
	vban::port_mapping port_mapping;
	vban::online_reps online_reps;
	vban::rep_crawler rep_crawler;
	vban::vote_processor vote_processor;
	unsigned warmed_up;
	vban::block_processor block_processor;
	vban::block_arrival block_arrival;
	vban::local_vote_history history;
	vban::keypair node_id;
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer vote_uniquer;
	vban::confirmation_height_processor confirmation_height_processor;
	vban::active_transactions active;
	vban::election_scheduler scheduler;
	vban::request_aggregator aggregator;
	vban::wallets wallets;
	const std::chrono::steady_clock::time_point startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	std::atomic<bool> unresponsive_work_peers{ false };
	std::atomic<bool> stopped{ false };
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
	// For tests only
	unsigned node_seq;
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (vban::block &);
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (vban::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> work_generate_blocking (vban::root const &);

private:
	void long_inactivity_cleanup ();
	void epoch_upgrader_impl (vban::raw_key const &, vban::epoch, uint64_t, uint64_t);
	vban::locked<std::future<void>> epoch_upgrading;
};

std::unique_ptr<container_info_component> collect_container_info (node & node, std::string const & name);

vban::node_flags const & inactive_node_flag_defaults ();

class node_wrapper final
{
public:
	node_wrapper (boost::filesystem::path const & path_a, boost::filesystem::path const & config_path_a, vban::node_flags const & node_flags_a);
	~node_wrapper ();

	std::shared_ptr<boost::asio::io_context> io_context;
	vban::work_pool work;
	std::shared_ptr<vban::node> node;
};

class inactive_node final
{
public:
	inactive_node (boost::filesystem::path const & path_a, vban::node_flags const & node_flags_a);
	inactive_node (boost::filesystem::path const & path_a, boost::filesystem::path const & config_path_a, vban::node_flags const & node_flags_a);

	vban::node_wrapper node_wrapper;
	std::shared_ptr<vban::node> node;
};
std::unique_ptr<vban::inactive_node> default_inactive_node (boost::filesystem::path const &, boost::program_options::variables_map const &);
}
