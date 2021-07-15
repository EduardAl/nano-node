#include <vban/node/testing.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST (gap_cache, add_new)
{
	vban::system system (1);
	vban::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<vban::send_block> (0, 1, 2, vban::keypair ().prv, 4, 5));
	cache.add (block1->hash ());
}

TEST (gap_cache, add_existing)
{
	vban::system system (1);
	vban::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<vban::send_block> (0, 1, 2, vban::keypair ().prv, 4, 5));
	cache.add (block1->hash ());
	vban::unique_lock<vban::mutex> lock (cache.mutex);
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	lock.unlock ();
	ASSERT_TIMELY (20s, arrival != std::chrono::steady_clock::now ());
	cache.add (block1->hash ());
	ASSERT_EQ (1, cache.size ());
	lock.lock ();
	auto existing2 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
}

TEST (gap_cache, comparison)
{
	vban::system system (1);
	vban::gap_cache cache (*system.nodes[0]);
	auto block1 (std::make_shared<vban::send_block> (1, 0, 2, vban::keypair ().prv, 4, 5));
	cache.add (block1->hash ());
	vban::unique_lock<vban::mutex> lock (cache.mutex);
	auto existing1 (cache.blocks.get<1> ().find (block1->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing1);
	auto arrival (existing1->arrival);
	lock.unlock ();
	ASSERT_TIMELY (20s, std::chrono::steady_clock::now () != arrival);
	auto block3 (std::make_shared<vban::send_block> (0, 42, 1, vban::keypair ().prv, 3, 4));
	cache.add (block3->hash ());
	ASSERT_EQ (2, cache.size ());
	lock.lock ();
	auto existing2 (cache.blocks.get<1> ().find (block3->hash ()));
	ASSERT_NE (cache.blocks.get<1> ().end (), existing2);
	ASSERT_GT (existing2->arrival, arrival);
	ASSERT_EQ (arrival, cache.blocks.get<1> ().begin ()->arrival);
}

// Upon receiving enough votes for a gapped block, a lazy bootstrap should be initiated
TEST (gap_cache, gap_bootstrap)
{
	vban::node_flags node_flags;
	node_flags.disable_legacy_bootstrap = true;
	node_flags.disable_request_loop = true; // to avoid fallback behavior of broadcasting blocks
	vban::system system (2, vban::transport::transport_type::tcp, node_flags);

	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	vban::block_hash latest (node1.latest (vban::dev_genesis_key.pub));
	vban::keypair key;
	auto send (std::make_shared<vban::send_block> (latest, key.pub, vban::genesis_amount - 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *system.work.generate (latest)));
	node1.process (*send);
	ASSERT_EQ (vban::genesis_amount - 100, node1.balance (vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, node2.balance (vban::genesis_account));
	// Confirm send block, allowing voting on the upcoming block
	node1.block_confirm (send);
	auto election = node1.active.election (send->qualified_root ());
	ASSERT_NE (nullptr, election);
	election->force_confirm ();
	ASSERT_TIMELY (2s, node1.block_confirmed (send->hash ()));
	node1.active.erase (*send);
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	auto latest_block (system.wallet (0)->send_action (vban::dev_genesis_key.pub, key.pub, 100));
	ASSERT_NE (nullptr, latest_block);
	ASSERT_EQ (vban::genesis_amount - 200, node1.balance (vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, node2.balance (vban::genesis_account));
	ASSERT_TIMELY (10s, node2.balance (vban::genesis_account) == vban::genesis_amount - 200);
}

TEST (gap_cache, two_dependencies)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::keypair key;
	vban::genesis genesis;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<vban::send_block> (send1->hash (), key.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *system.work.generate (send1->hash ())));
	auto open (std::make_shared<vban::open_block> (send1->hash (), key.pub, key.pub, key.prv, key.pub, *system.work.generate (key.pub)));
	ASSERT_EQ (0, node1.gap_cache.size ());
	node1.block_processor.add (send2, vban::seconds_since_epoch ());
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.gap_cache.size ());
	node1.block_processor.add (open, vban::seconds_since_epoch ());
	node1.block_processor.flush ();
	ASSERT_EQ (2, node1.gap_cache.size ());
	node1.block_processor.add (send1, vban::seconds_since_epoch ());
	node1.block_processor.flush ();
	ASSERT_EQ (0, node1.gap_cache.size ());
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_TRUE (node1.store.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, send2->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, open->hash ()));
}
