#include <vban/node/node.hpp>
#include <vban/node/testing.hpp>
#include <vban/test_common/network.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <future>

std::shared_ptr<vban::transport::channel_tcp> vban::establish_tcp (vban::system & system, vban::node & node, vban::endpoint const & endpoint)
{
	using namespace std::chrono_literals;
	debug_assert (node.network.endpoint () != endpoint && "Establishing TCP to self is not allowed");

	std::shared_ptr<vban::transport::channel_tcp> result;
	debug_assert (!node.flags.disable_tcp_realtime);
	std::promise<std::shared_ptr<vban::transport::channel>> promise;
	auto callback = [&promise] (std::shared_ptr<vban::transport::channel> channel_a) { promise.set_value (channel_a); };
	auto future = promise.get_future ();
	node.network.tcp_channels.start_tcp (endpoint, callback);
	auto error = system.poll_until_true (2s, [&future] { return future.wait_for (0s) == std::future_status::ready; });
	if (!error)
	{
		auto channel = future.get ();
		EXPECT_NE (nullptr, channel);
		if (channel)
		{
			result = node.network.tcp_channels.find_channel (channel->get_tcp_endpoint ());
		}
	}
	return result;
}

std::function<void (std::shared_ptr<vban::transport::channel> channel_a)> vban::keepalive_tcp_callback (vban::node & node_a)
{
	return [node_w = std::weak_ptr<vban::node> (node_a.shared ())] (std::shared_ptr<vban::transport::channel> channel_a) {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.send_keepalive (channel_a);
		};
	};
}
