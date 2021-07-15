#pragma once

#include <vban/node/bootstrap/bootstrap_attempt.hpp>

#include <boost/property_tree/ptree_fwd.hpp>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

namespace vban
{
class node;

class bootstrap_attempt_legacy : public bootstrap_attempt
{
public:
	explicit bootstrap_attempt_legacy (std::shared_ptr<vban::node> const & node_a, uint64_t const incremental_id_a, std::string const & id_a, uint32_t const frontiers_age_a, vban::account const & start_account_a);
	void run () override;
	bool consume_future (std::future<bool> &);
	void stop () override;
	bool request_frontier (vban::unique_lock<vban::mutex> &, bool = false);
	void request_push (vban::unique_lock<vban::mutex> &);
	void add_frontier (vban::pull_info const &) override;
	void add_bulk_push_target (vban::block_hash const &, vban::block_hash const &) override;
	bool request_bulk_push_target (std::pair<vban::block_hash, vban::block_hash> &) override;
	void set_start_account (vban::account const &) override;
	void run_start (vban::unique_lock<vban::mutex> &);
	void get_information (boost::property_tree::ptree &) override;
	vban::tcp_endpoint endpoint_frontier_request;
	std::weak_ptr<vban::frontier_req_client> frontiers;
	std::weak_ptr<vban::bulk_push_client> push;
	std::deque<vban::pull_info> frontier_pulls;
	std::vector<std::pair<vban::block_hash, vban::block_hash>> bulk_push_targets;
	vban::account start_account{ 0 };
	std::atomic<unsigned> account_count{ 0 };
	uint32_t frontiers_age;
};
}
