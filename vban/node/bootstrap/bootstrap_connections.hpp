#pragma once

#include <vban/node/bootstrap/bootstrap_bulk_pull.hpp>
#include <vban/node/common.hpp>
#include <vban/node/socket.hpp>

#include <atomic>

namespace vban
{
class node;
namespace transport
{
	class channel_tcp;
}

class bootstrap_attempt;
class bootstrap_connections;
class frontier_req_client;
class pull_info;
class bootstrap_client final : public std::enable_shared_from_this<bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr<vban::node> const & node_a, std::shared_ptr<vban::bootstrap_connections> const & connections_a, std::shared_ptr<vban::transport::channel_tcp> const & channel_a, std::shared_ptr<vban::socket> const & socket_a);
	~bootstrap_client ();
	std::shared_ptr<vban::bootstrap_client> shared ();
	void stop (bool force);
	double sample_block_rate ();
	double elapsed_seconds () const;
	void set_start_time (std::chrono::steady_clock::time_point start_time_a);
	std::shared_ptr<vban::node> node;
	std::shared_ptr<vban::bootstrap_connections> connections;
	std::shared_ptr<vban::transport::channel_tcp> channel;
	std::shared_ptr<vban::socket> socket;
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::atomic<uint64_t> block_count{ 0 };
	std::atomic<double> block_rate{ 0 };
	std::atomic<bool> pending_stop{ false };
	std::atomic<bool> hard_stop{ false };

private:
	mutable vban::mutex start_time_mutex;
	std::chrono::steady_clock::time_point start_time_m;
};

class bootstrap_connections final : public std::enable_shared_from_this<bootstrap_connections>
{
public:
	bootstrap_connections (vban::node & node_a);
	std::shared_ptr<vban::bootstrap_connections> shared ();
	std::shared_ptr<vban::bootstrap_client> connection (std::shared_ptr<vban::bootstrap_attempt> const & attempt_a = nullptr, bool use_front_connection = false);
	void pool_connection (std::shared_ptr<vban::bootstrap_client> const & client_a, bool new_client = false, bool push_front = false);
	void add_connection (vban::endpoint const & endpoint_a);
	std::shared_ptr<vban::bootstrap_client> find_connection (vban::tcp_endpoint const & endpoint_a);
	void connect_client (vban::tcp_endpoint const & endpoint_a, bool push_front = false);
	unsigned target_connections (size_t pulls_remaining, size_t attempts_count);
	void populate_connections (bool repeat = true);
	void start_populate_connections ();
	void add_pull (vban::pull_info const & pull_a);
	void request_pull (vban::unique_lock<vban::mutex> & lock_a);
	void requeue_pull (vban::pull_info const & pull_a, bool network_error = false);
	void clear_pulls (uint64_t);
	void run ();
	void stop ();
	std::deque<std::weak_ptr<vban::bootstrap_client>> clients;
	std::atomic<unsigned> connections_count{ 0 };
	vban::node & node;
	std::deque<std::shared_ptr<vban::bootstrap_client>> idle;
	std::deque<vban::pull_info> pulls;
	std::atomic<bool> populate_connections_started{ false };
	std::atomic<bool> new_connections_empty{ false };
	std::atomic<bool> stopped{ false };
	vban::mutex mutex;
	vban::condition_variable condition;
};
}
