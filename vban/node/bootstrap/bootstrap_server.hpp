#pragma once

#include <vban/node/common.hpp>
#include <vban/node/socket.hpp>

#include <atomic>
#include <queue>

namespace vban
{
class bootstrap_server;
class bootstrap_listener final
{
public:
	bootstrap_listener (uint16_t, vban::node &);
	void start ();
	void stop ();
	void accept_action (boost::system::error_code const &, std::shared_ptr<vban::socket> const &);
	size_t connection_count ();

	vban::mutex mutex;
	std::unordered_map<vban::bootstrap_server *, std::weak_ptr<vban::bootstrap_server>> connections;
	vban::tcp_endpoint endpoint ();
	vban::node & node;
	std::shared_ptr<vban::server_socket> listening_socket;
	bool on{ false };
	std::atomic<size_t> bootstrap_count{ 0 };
	std::atomic<size_t> realtime_count{ 0 };

private:
	uint16_t port;
};

std::unique_ptr<container_info_component> collect_container_info (bootstrap_listener & bootstrap_listener, std::string const & name);

class message;
enum class bootstrap_server_type
{
	undefined,
	bootstrap,
	realtime,
	realtime_response_server // special type for tcp channel response server
};
class bootstrap_server final : public std::enable_shared_from_this<vban::bootstrap_server>
{
public:
	bootstrap_server (std::shared_ptr<vban::socket> const &, std::shared_ptr<vban::node> const &);
	~bootstrap_server ();
	void stop ();
	void receive ();
	void receive_header_action (boost::system::error_code const &, size_t);
	void receive_bulk_pull_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_bulk_pull_account_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_frontier_req_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_keepalive_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_publish_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_confirm_req_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_confirm_ack_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_node_id_handshake_action (boost::system::error_code const &, size_t, vban::message_header const &);
	void receive_telemetry_ack_action (boost::system::error_code const & ec, size_t size_a, vban::message_header const & header_a);
	void add_request (std::unique_ptr<vban::message>);
	void finish_request ();
	void finish_request_async ();
	void timeout ();
	void run_next (vban::unique_lock<vban::mutex> & lock_a);
	bool is_bootstrap_connection ();
	bool is_realtime_connection ();
	std::shared_ptr<std::vector<uint8_t>> receive_buffer;
	std::shared_ptr<vban::socket> socket;
	std::shared_ptr<vban::node> node;
	vban::mutex mutex;
	std::queue<std::unique_ptr<vban::message>> requests;
	std::atomic<bool> stopped{ false };
	std::atomic<vban::bootstrap_server_type> type{ vban::bootstrap_server_type::undefined };
	// Remote enpoint used to remove response channel even after socket closing
	vban::tcp_endpoint remote_endpoint{ boost::asio::ip::address_v6::any (), 0 };
	vban::account remote_node_id{ 0 };
	std::chrono::steady_clock::time_point last_telemetry_req{ std::chrono::steady_clock::time_point () };
};
}
