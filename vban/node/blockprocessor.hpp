#pragma once

#include <vban/lib/blocks.hpp>
#include <vban/node/state_block_signature_verification.hpp>
#include <vban/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <chrono>
#include <memory>
#include <thread>
#include <unordered_set>

namespace vban
{
class node;
class read_transaction;
class transaction;
class write_transaction;
class write_database_queue;

enum class block_origin
{
	local,
	remote
};

class block_post_events final
{
public:
	explicit block_post_events (std::function<vban::read_transaction ()> &&);
	~block_post_events ();
	std::deque<std::function<void (vban::read_transaction const &)>> events;

private:
	std::function<vban::read_transaction ()> get_transaction;
};

/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	explicit block_processor (vban::node &, vban::write_database_queue &);
	~block_processor ();
	void stop ();
	void flush ();
	size_t size ();
	bool full ();
	bool half_full ();
	void add_local (vban::unchecked_info const & info_a);
	void add (vban::unchecked_info const &);
	void add (std::shared_ptr<vban::block> const &, uint64_t = 0);
	void force (std::shared_ptr<vban::block> const &);
	void update (std::shared_ptr<vban::block> const &);
	void wait_write ();
	bool should_log ();
	bool have_blocks_ready ();
	bool have_blocks ();
	void process_blocks ();
	vban::process_return process_one (vban::write_transaction const &, block_post_events &, vban::unchecked_info, const bool = false, vban::block_origin const = vban::block_origin::remote);
	vban::process_return process_one (vban::write_transaction const &, block_post_events &, std::shared_ptr<vban::block> const &);
	std::atomic<bool> flushing{ false };
	// Delay required for average network propagartion before requesting confirmation
	static std::chrono::milliseconds constexpr confirmation_request_delay{ 1500 };

private:
	void queue_unchecked (vban::write_transaction const &, vban::hash_or_account const &);
	void process_batch (vban::unique_lock<vban::mutex> &);
	void process_live (vban::transaction const &, vban::block_hash const &, std::shared_ptr<vban::block> const &, vban::process_return const &, vban::block_origin const = vban::block_origin::remote);
	void process_old (vban::transaction const &, std::shared_ptr<vban::block> const &, vban::block_origin const);
	void requeue_invalid (vban::block_hash const &, vban::unchecked_info const &);
	void process_verified_state_blocks (std::deque<vban::unchecked_info> &, std::vector<int> const &, std::vector<vban::block_hash> const &, std::vector<vban::signature> const &);
	bool stopped{ false };
	bool active{ false };
	bool awaiting_write{ false };
	std::chrono::steady_clock::time_point next_log;
	std::deque<vban::unchecked_info> blocks;
	std::deque<std::shared_ptr<vban::block>> forced;
	std::deque<std::shared_ptr<vban::block>> updates;
	vban::condition_variable condition;
	vban::node & node;
	vban::write_database_queue & write_database_queue;
	vban::mutex mutex{ mutex_identifier (mutexes::block_processor) };
	vban::state_block_signature_verification state_block_signature_verification;
	std::thread processing_thread;

	friend std::unique_ptr<container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
};
std::unique_ptr<vban::container_info_component> collect_container_info (block_processor & block_processor, std::string const & name);
}
