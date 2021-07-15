#pragma once

#include <vban/node/common.hpp>

#include <deque>
#include <future>

namespace vban
{
class bootstrap_attempt;
class bootstrap_client;
class frontier_req_client final : public std::enable_shared_from_this<vban::frontier_req_client>
{
public:
	explicit frontier_req_client (std::shared_ptr<vban::bootstrap_client> const &, std::shared_ptr<vban::bootstrap_attempt> const &);
	void run (vban::account const & start_account_a, uint32_t const frontiers_age_a, uint32_t const count_a);
	void receive_frontier ();
	void received_frontier (boost::system::error_code const &, size_t);
	bool bulk_push_available ();
	void unsynced (vban::block_hash const &, vban::block_hash const &);
	void next ();
	std::shared_ptr<vban::bootstrap_client> connection;
	std::shared_ptr<vban::bootstrap_attempt> attempt;
	vban::account current;
	vban::block_hash frontier;
	unsigned count;
	vban::account last_account{ std::numeric_limits<vban::uint256_t>::max () }; // Using last possible account stop further frontier requests
	std::chrono::steady_clock::time_point start_time;
	std::promise<bool> promise;
	/** A very rough estimate of the cost of `bulk_push`ing missing blocks */
	uint64_t bulk_push_cost;
	std::deque<std::pair<vban::account, vban::block_hash>> accounts;
	uint32_t frontiers_age{ std::numeric_limits<uint32_t>::max () };
	uint32_t count_limit{ std::numeric_limits<uint32_t>::max () };
	static size_t constexpr size_frontier = sizeof (vban::account) + sizeof (vban::block_hash);
};
class bootstrap_server;
class frontier_req;
class frontier_req_server final : public std::enable_shared_from_this<vban::frontier_req_server>
{
public:
	frontier_req_server (std::shared_ptr<vban::bootstrap_server> const &, std::unique_ptr<vban::frontier_req>);
	void send_next ();
	void sent_action (boost::system::error_code const &, size_t);
	void send_finished ();
	void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
	bool send_confirmed ();
	std::shared_ptr<vban::bootstrap_server> connection;
	vban::account current;
	vban::block_hash frontier;
	std::unique_ptr<vban::frontier_req> request;
	size_t count;
	std::deque<std::pair<vban::account, vban::block_hash>> accounts;
};
}
