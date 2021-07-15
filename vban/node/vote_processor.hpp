#pragma once

#include <vban/lib/numbers.hpp>
#include <vban/lib/utility.hpp>
#include <vban/secure/common.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace vban
{
class signature_checker;
class active_transactions;
class block_store;
class node_observers;
class stats;
class node_config;
class logger_mt;
class online_reps;
class rep_crawler;
class ledger;
class network_params;
class node_flags;

class transaction;
namespace transport
{
	class channel;
}

class vote_processor final
{
public:
	explicit vote_processor (vban::signature_checker & checker_a, vban::active_transactions & active_a, vban::node_observers & observers_a, vban::stat & stats_a, vban::node_config & config_a, vban::node_flags & flags_a, vban::logger_mt & logger_a, vban::online_reps & online_reps_a, vban::rep_crawler & rep_crawler_a, vban::ledger & ledger_a, vban::network_params & network_params_a);
	/** Returns false if the vote was processed */
	bool vote (std::shared_ptr<vban::vote> const &, std::shared_ptr<vban::transport::channel> const &);
	/** Note: node.active.mutex lock is required */
	vban::vote_code vote_blocking (std::shared_ptr<vban::vote> const &, std::shared_ptr<vban::transport::channel> const &, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<vban::vote>, std::shared_ptr<vban::transport::channel>>> const &);
	void flush ();
	/** Block until the currently active processing cycle finishes */
	void flush_active ();
	size_t size ();
	bool empty ();
	bool half_full ();
	void calculate_weights ();
	void stop ();
	std::atomic<uint64_t> total_processed{ 0 };

private:
	void process_loop ();

	vban::signature_checker & checker;
	vban::active_transactions & active;
	vban::node_observers & observers;
	vban::stat & stats;
	vban::node_config & config;
	vban::logger_mt & logger;
	vban::online_reps & online_reps;
	vban::rep_crawler & rep_crawler;
	vban::ledger & ledger;
	vban::network_params & network_params;
	size_t max_votes;
	std::deque<std::pair<std::shared_ptr<vban::vote>, std::shared_ptr<vban::transport::channel>>> votes;
	/** Representatives levels for random early detection */
	std::unordered_set<vban::account> representatives_1;
	std::unordered_set<vban::account> representatives_2;
	std::unordered_set<vban::account> representatives_3;
	vban::condition_variable condition;
	vban::mutex mutex{ mutex_identifier (mutexes::vote_processor) };
	bool started;
	bool stopped;
	bool is_active;
	std::thread thread;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, std::string const & name);
	friend class vote_processor_weights_Test;
};

std::unique_ptr<container_info_component> collect_container_info (vote_processor & vote_processor, std::string const & name);
}
