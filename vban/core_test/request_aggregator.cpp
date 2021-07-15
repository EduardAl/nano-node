#include <vban/lib/jsonconfig.hpp>
#include <vban/node/request_aggregator.hpp>
#include <vban/node/testing.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (request_aggregator, one)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	auto send1 (std::make_shared<vban::state_block> (vban::dev_genesis_key.pub, genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	// Not yet in the ledger
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// Already cached
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::message, vban::stat::detail::confirm_ack, vban::stat::dir::out));
}

TEST (request_aggregator, one_update)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	vban::keypair key1;
	auto send1 = vban::state_block_builder ()
				 .account (vban::dev_genesis_key.pub)
				 .previous (genesis.hash ())
				 .representative (vban::dev_genesis_key.pub)
				 .balance (vban::genesis_amount - vban::Gxrb_ratio)
				 .link (vban::dev_genesis_key.pub)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*node.work_generate_blocking (genesis.hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.confirmation_height_processor.add (send1);
	ASSERT_TIMELY (5s, node.ledger.block_confirmed (node.store.tx_begin_read (), send1->hash ()));
	auto send2 = vban::state_block_builder ()
				 .account (vban::dev_genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (vban::dev_genesis_key.pub)
				 .balance (vban::genesis_amount - 2 * vban::Gxrb_ratio)
				 .link (vban::dev_genesis_key.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	auto receive1 = vban::state_block_builder ()
					.account (key1.pub)
					.previous (0)
					.representative (vban::dev_genesis_key.pub)
					.balance (vban::Gxrb_ratio)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build_shared ();
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *receive1).code);
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	request.clear ();
	request.emplace_back (receive1->hash (), receive1->root ());
	// Update the pool of requests with another hash
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 0 < node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes))
	ASSERT_TRUE (node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_hashes));
	size_t count = 0;
	ASSERT_TIMELY (3s, 1 == (count = node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes)));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::message, vban::stat::detail::confirm_ack, vban::stat::dir::out));
}

TEST (request_aggregator, two)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	vban::keypair key1;
	vban::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (vban::dev_genesis_key.pub)
				 .previous (genesis.hash ())
				 .representative (vban::dev_genesis_key.pub)
				 .balance (vban::genesis_amount - 1)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*node.work_generate_blocking (genesis.hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	node.confirmation_height_processor.add (send1);
	ASSERT_TIMELY (5s, node.ledger.block_confirmed (node.store.tx_begin_read (), send1->hash ()));
	auto send2 = builder.make_block ()
				 .account (vban::dev_genesis_key.pub)
				 .previous (send1->hash ())
				 .representative (vban::dev_genesis_key.pub)
				 .balance (vban::genesis_amount - 2)
				 .link (vban::dev_genesis_key.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*node.work_generate_blocking (send1->hash ()))
				 .build_shared ();
	auto receive1 = builder.make_block ()
					.account (key1.pub)
					.previous (0)
					.representative (vban::dev_genesis_key.pub)
					.balance (1)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*node.work_generate_blocking (key1.pub))
					.build_shared ();
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send2).code);
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *receive1).code);
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send2->hash (), send2->root ());
	request.emplace_back (receive1->hash (), receive1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	// Process both blocks
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// One vote should be generated for both blocks
	ASSERT_TIMELY (3s, 0 < node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// The same request should now send the cached vote
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::message, vban::stat::detail::confirm_ack, vban::stat::dir::out));
	// Make sure the cached vote is for both hashes
	auto vote1 (node.history.votes (send2->root (), send2->hash ()));
	auto vote2 (node.history.votes (receive1->root (), receive1->hash ()));
	ASSERT_EQ (1, vote1.size ());
	ASSERT_EQ (1, vote2.size ());
	ASSERT_EQ (vote1.front (), vote2.front ());
}

TEST (request_aggregator, two_endpoints)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	vban::node_flags node_flags;
	node_flags.disable_rep_crawler = true;
	auto & node1 (*system.add_node (node_config, node_flags));
	node_config.peering_port = vban::get_available_port ();
	auto & node2 (*system.add_node (node_config, node_flags));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	auto send1 (std::make_shared<vban::state_block> (vban::dev_genesis_key.pub, genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - 1, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *node1.work_generate_blocking (genesis.hash ())));
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (node1.store.tx_begin_write (), *send1).code);
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	auto channel2 (node2.network.udp_channels.create (node2.network.endpoint ()));
	ASSERT_NE (vban::transport::map_endpoint_to_v6 (channel1->get_endpoint ()), vban::transport::map_endpoint_to_v6 (channel2->get_endpoint ()));
	// Use the aggregator from node1 only, making requests from both nodes
	node1.aggregator.add (channel1, request);
	node1.aggregator.add (channel2, request);
	ASSERT_EQ (2, node1.aggregator.size ());
	// For the first request it generates the vote, for the second it uses the generated vote
	ASSERT_TIMELY (3s, node1.aggregator.empty ());
	ASSERT_EQ (2, node1.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node1.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 0 == node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_hashes) + node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_late_hashes));
	ASSERT_TIMELY (3s, 1 == node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_votes) + node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_late_votes));
	ASSERT_TIMELY (3s, 0 == node1.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
}

TEST (request_aggregator, split)
{
	constexpr size_t max_vbh = vban::network::confirm_ack_hashes_max;
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	std::vector<std::shared_ptr<vban::block>> blocks;
	auto previous = genesis.hash ();
	// Add max_vbh + 1 blocks and request votes for them
	for (size_t i (0); i <= max_vbh; ++i)
	{
		vban::block_builder builder;
		blocks.push_back (builder
						  .state ()
						  .account (vban::dev_genesis_key.pub)
						  .previous (previous)
						  .representative (vban::dev_genesis_key.pub)
						  .balance (vban::genesis_amount - (i + 1))
						  .link (vban::dev_genesis_key.pub)
						  .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
						  .work (*system.work.generate (previous))
						  .build ());
		auto const & block = blocks.back ();
		previous = block->hash ();
		ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *block).code);
		request.emplace_back (block->hash (), block->root ());
	}
	// Confirm all blocks
	node.block_confirm (blocks.back ());
	auto election (node.active.election (blocks.back ()->qualified_root ()));
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (5s, max_vbh + 2 == node.ledger.cache.cemented_count);
	ASSERT_EQ (max_vbh + 1, request.size ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	// In the ledger but no vote generated yet
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_TRUE (node.aggregator.empty ());
	// Two votes were sent, the first one for 12 hashes and the second one for 1 hash
	ASSERT_EQ (1, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 13 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_hashes));
	ASSERT_TIMELY (3s, 0 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::message, vban::stat::detail::confirm_ack, vban::stat::dir::out));
}

TEST (request_aggregator, channel_lifetime)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	auto send1 (std::make_shared<vban::state_block> (vban::dev_genesis_key.pub, genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	{
		// The aggregator should extend the lifetime of the channel
		auto channel (node.network.udp_channels.create (node.network.endpoint ()));
		node.aggregator.add (channel, request);
	}
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
}

TEST (request_aggregator, channel_update)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	auto send1 (std::make_shared<vban::state_block> (vban::dev_genesis_key.pub, genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	std::weak_ptr<vban::transport::channel> channel1_w;
	{
		auto channel1 (node.network.udp_channels.create (node.network.endpoint ()));
		channel1_w = channel1;
		node.aggregator.add (channel1, request);
		auto channel2 (node.network.udp_channels.create (node.network.endpoint ()));
		// The aggregator then hold channel2 and drop channel1
		node.aggregator.add (channel2, request);
	}
	// Both requests were for the same endpoint, so only one pool should exist
	ASSERT_EQ (1, node.aggregator.size ());
	// channel1 is not being held anymore
	ASSERT_EQ (nullptr, channel1_w.lock ());
	ASSERT_TIMELY (3s, 0 < node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes) == 0);
}

TEST (request_aggregator, channel_max_queue)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	node_config.max_queued_requests = 1;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	auto send1 (std::make_shared<vban::state_block> (vban::dev_genesis_key.pub, genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
}

TEST (request_aggregator, unique)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node (*system.add_node (node_config));
	vban::genesis genesis;
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	auto send1 (std::make_shared<vban::state_block> (vban::dev_genesis_key.pub, genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *node.work_generate_blocking (genesis.hash ())));
	ASSERT_EQ (vban::process_result::progress, node.ledger.process (node.store.tx_begin_write (), *send1).code);
	std::vector<std::pair<vban::block_hash, vban::root>> request;
	request.emplace_back (send1->hash (), send1->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	node.aggregator.add (channel, request);
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
}

namespace vban
{
TEST (request_aggregator, cannot_vote)
{
	vban::system system;
	vban::node_flags flags;
	flags.disable_request_loop = true;
	auto & node (*system.add_node (flags));
	// This prevents activation of blocks which are cemented
	node.confirmation_height_processor.cemented_observers.clear ();
	vban::genesis genesis;
	vban::state_block_builder builder;
	auto send1 = builder.make_block ()
				 .account (vban::dev_genesis_key.pub)
				 .previous (vban::genesis_hash)
				 .representative (vban::dev_genesis_key.pub)
				 .balance (vban::genesis_amount - 1)
				 .link (vban::dev_genesis_key.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*system.work.generate (vban::genesis_hash))
				 .build_shared ();
	auto send2 = builder.make_block ()
				 .from (*send1)
				 .previous (send1->hash ())
				 .balance (send1->balance ().number () - 1)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*system.work.generate (send1->hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, node.process (*send1).code);
	ASSERT_EQ (vban::process_result::progress, node.process (*send2).code);
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	ASSERT_FALSE (node.ledger.dependents_confirmed (node.store.tx_begin_read (), *send2));

	std::vector<std::pair<vban::block_hash, vban::root>> request;
	// Correct hash, correct root
	request.emplace_back (send2->hash (), send2->root ());
	// Incorrect hash, correct root
	request.emplace_back (1, send2->root ());
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (1, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 2 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::message, vban::stat::detail::confirm_ack, vban::stat::dir::out));

	// With an ongoing election
	node.block_confirm (send2);
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (2, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_TIMELY (3s, 4 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cached_votes));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::message, vban::stat::detail::confirm_ack, vban::stat::dir::out));

	// Confirm send1
	node.block_confirm (send1);
	auto election (node.active.election (send1->qualified_root ()));
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (3s, node.ledger.dependents_confirmed (node.store.tx_begin_read (), *send2));
	node.aggregator.add (channel, request);
	ASSERT_EQ (1, node.aggregator.size ());
	ASSERT_TIMELY (3s, node.aggregator.empty ());
	ASSERT_EQ (3, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_accepted));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::aggregator, vban::stat::detail::aggregator_dropped));
	ASSERT_EQ (4, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_cannot_vote));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_hashes));
	ASSERT_TIMELY (3s, 1 == node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_generated_votes));
	ASSERT_EQ (0, node.stats.count (vban::stat::type::requests, vban::stat::detail::requests_unknown));
	ASSERT_TIMELY (3s, 1 <= node.stats.count (vban::stat::type::message, vban::stat::detail::confirm_ack, vban::stat::dir::out));
}
}
