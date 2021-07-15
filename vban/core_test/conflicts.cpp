#include <vban/node/election.hpp>
#include <vban/node/testing.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/variant/get.hpp>

using namespace std::chrono_literals;

TEST (conflicts, start_stop)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (0, node1.active.size ());
	node1.scheduler.activate (vban::dev_genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, election1->votes ().size ());
}

TEST (conflicts, add_existing)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send1).code);
	node1.scheduler.activate (vban::dev_genesis_key.pub, node1.store.tx_begin_read ());
	vban::keypair key2;
	auto send2 (std::make_shared<vban::send_block> (genesis.hash (), key2.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	send2->sideband_set ({});
	node1.scheduler.activate (vban::dev_genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election1);
	ASSERT_EQ (1, node1.active.size ());
	auto vote1 (std::make_shared<vban::vote> (key2.pub, key2.prv, 0, send2));
	node1.active.vote (vote1);
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes (election1->votes ());
	ASSERT_NE (votes.end (), votes.find (key2.pub));
}

TEST (conflicts, add_two)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send1).code);
	node1.block_confirm (send1);
	node1.active.election (send1->qualified_root ())->force_confirm ();
	vban::keypair key2;
	auto send2 (std::make_shared<vban::send_block> (send1->hash (), key2.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send2).code);
	node1.scheduler.activate (vban::dev_genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	ASSERT_EQ (2, node1.active.size ());
}

TEST (vote_uniquer, null)
{
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer uniquer (block_uniquer);
	ASSERT_EQ (nullptr, uniquer.unique (nullptr));
}

// Show that an identical vote can be uniqued
TEST (vote_uniquer, same_vote)
{
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer uniquer (block_uniquer);
	vban::keypair key;
	auto vote1 (std::make_shared<vban::vote> (key.pub, key.prv, 0, std::make_shared<vban::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote2 (std::make_shared<vban::vote> (*vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

// Show that a different vote for the same block will have the block uniqued
TEST (vote_uniquer, same_block)
{
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer uniquer (block_uniquer);
	vban::keypair key1;
	vban::keypair key2;
	auto block1 (std::make_shared<vban::state_block> (0, 0, 0, 0, 0, key1.prv, key1.pub, 0));
	auto block2 (std::make_shared<vban::state_block> (*block1));
	auto vote1 (std::make_shared<vban::vote> (key1.pub, key1.prv, 0, block1));
	auto vote2 (std::make_shared<vban::vote> (key1.pub, key1.prv, 0, block2));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
	ASSERT_NE (vote1, vote2);
	ASSERT_EQ (boost::get<std::shared_ptr<vban::block>> (vote1->blocks[0]), boost::get<std::shared_ptr<vban::block>> (vote2->blocks[0]));
}

TEST (vote_uniquer, vbh_one)
{
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer uniquer (block_uniquer);
	vban::keypair key;
	auto block (std::make_shared<vban::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<vban::block_hash> hashes;
	hashes.push_back (block->hash ());
	auto vote1 (std::make_shared<vban::vote> (key.pub, key.prv, 0, hashes));
	auto vote2 (std::make_shared<vban::vote> (*vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote1, uniquer.unique (vote2));
}

TEST (vote_uniquer, vbh_two)
{
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer uniquer (block_uniquer);
	vban::keypair key;
	auto block1 (std::make_shared<vban::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<vban::block_hash> hashes1;
	hashes1.push_back (block1->hash ());
	auto block2 (std::make_shared<vban::state_block> (1, 0, 0, 0, 0, key.prv, key.pub, 0));
	std::vector<vban::block_hash> hashes2;
	hashes2.push_back (block2->hash ());
	auto vote1 (std::make_shared<vban::vote> (key.pub, key.prv, 0, hashes1));
	auto vote2 (std::make_shared<vban::vote> (key.pub, key.prv, 0, hashes2));
	ASSERT_EQ (vote1, uniquer.unique (vote1));
	ASSERT_EQ (vote2, uniquer.unique (vote2));
}

TEST (vote_uniquer, cleanup)
{
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer uniquer (block_uniquer);
	vban::keypair key;
	auto vote1 (std::make_shared<vban::vote> (key.pub, key.prv, 0, std::make_shared<vban::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote2 (std::make_shared<vban::vote> (key.pub, key.prv, 1, std::make_shared<vban::state_block> (0, 0, 0, 0, 0, key.prv, key.pub, 0)));
	auto vote3 (uniquer.unique (vote1));
	auto vote4 (uniquer.unique (vote2));
	vote2.reset ();
	vote4.reset ();
	ASSERT_EQ (2, uniquer.size ());
	auto iterations (0);
	while (uniquer.size () == 2)
	{
		auto vote5 (uniquer.unique (vote1));
		ASSERT_LT (iterations++, 200);
	}
}
