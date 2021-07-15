#pragma once

#include <vban/node/bootstrap/bootstrap.hpp>

#include <atomic>
#include <future>

namespace vban
{
class node;

class frontier_req_client;
class bulk_push_client;
class bootstrap_attempt : public std::enable_shared_from_this<bootstrap_attempt>
{
public:
	explicit bootstrap_attempt (std::shared_ptr<vban::node> const & node_a, vban::bootstrap_mode mode_a, uint64_t incremental_id_a, std::string id_a);
	virtual ~bootstrap_attempt ();
	virtual void run () = 0;
	virtual void stop ();
	bool still_pulling ();
	void pull_started ();
	void pull_finished ();
	bool should_log ();
	std::string mode_text ();
	virtual void add_frontier (vban::pull_info const &);
	virtual void add_bulk_push_target (vban::block_hash const &, vban::block_hash const &);
	virtual bool request_bulk_push_target (std::pair<vban::block_hash, vban::block_hash> &);
	virtual void set_start_account (vban::account const &);
	virtual bool lazy_start (vban::hash_or_account const &, bool confirmed = true);
	virtual void lazy_add (vban::pull_info const &);
	virtual void lazy_requeue (vban::block_hash const &, vban::block_hash const &, bool);
	virtual uint32_t lazy_batch_size ();
	virtual bool lazy_has_expired () const;
	virtual bool lazy_processed_or_exists (vban::block_hash const &);
	virtual bool process_block (std::shared_ptr<vban::block> const &, vban::account const &, uint64_t, vban::bulk_pull::count_t, bool, unsigned);
	virtual void requeue_pending (vban::account const &);
	virtual void wallet_start (std::deque<vban::account> &);
	virtual size_t wallet_size ();
	virtual void get_information (boost::property_tree::ptree &) = 0;
	vban::mutex next_log_mutex;
	std::chrono::steady_clock::time_point next_log{ std::chrono::steady_clock::now () };
	std::atomic<unsigned> pulling{ 0 };
	std::shared_ptr<vban::node> node;
	std::atomic<uint64_t> total_blocks{ 0 };
	std::atomic<unsigned> requeued_pulls{ 0 };
	std::atomic<bool> started{ false };
	std::atomic<bool> stopped{ false };
	uint64_t incremental_id{ 0 };
	std::string id;
	std::chrono::steady_clock::time_point attempt_start{ std::chrono::steady_clock::now () };
	std::atomic<bool> frontiers_received{ false };
	vban::bootstrap_mode mode;
	vban::mutex mutex;
	vban::condition_variable condition;
};
}
