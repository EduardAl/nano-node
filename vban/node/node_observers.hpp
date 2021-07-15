#pragma once

#include <vban/lib/numbers.hpp>
#include <vban/lib/utility.hpp>
#include <vban/node/active_transactions.hpp>
#include <vban/node/transport/transport.hpp>

namespace vban
{
class telemetry;
class node_observers final
{
public:
	using blocks_t = vban::observer_set<vban::election_status const &, std::vector<vban::vote_with_weight_info> const &, vban::account const &, vban::uint256_t const &, bool>;
	blocks_t blocks;
	vban::observer_set<bool> wallet;
	vban::observer_set<std::shared_ptr<vban::vote>, std::shared_ptr<vban::transport::channel>, vban::vote_code> vote;
	vban::observer_set<vban::block_hash const &> active_stopped;
	vban::observer_set<vban::account const &, bool> account_balance;
	vban::observer_set<std::shared_ptr<vban::transport::channel>> endpoint;
	vban::observer_set<> disconnect;
	vban::observer_set<vban::root const &> work_cancel;
	vban::observer_set<vban::telemetry_data const &, vban::endpoint const &> telemetry;
};

std::unique_ptr<container_info_component> collect_container_info (node_observers & node_observers, std::string const & name);
}
