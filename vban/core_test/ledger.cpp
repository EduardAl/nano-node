#include <vban/lib/stats.hpp>
#include <vban/lib/threading.hpp>
#include <vban/node/election.hpp>
#include <vban/node/rocksdb/rocksdb.hpp>
#include <vban/node/testing.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// Init returns an error if it can't open files at the path
TEST (ledger, store_error)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	vban::logger_mt logger;
	vban::mdb_store store (logger, boost::filesystem::path ("///"));
	ASSERT_TRUE (store.init_error ());
	vban::stat stats;
	vban::ledger ledger (store, stats);
}

// Ledger can be initialized and returns a basic query for an empty account
TEST (ledger, empty)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::account account;
	auto transaction (store->tx_begin_read ());
	auto balance (ledger.account_balance (transaction, account));
	ASSERT_TRUE (balance.is_zero ());
}

// Genesis account should have the max balance on empty initialization
TEST (ledger, genesis_balance)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	auto balance (ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, balance);
	auto amount (ledger.amount (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, amount);
	vban::account_info info;
	ASSERT_FALSE (store->account_get (transaction, vban::genesis_account, info));
	ASSERT_EQ (1, ledger.cache.account_count);
	// Frontier time should have been updated when genesis balance was added
	ASSERT_GE (vban::seconds_since_epoch (), info.modified);
	ASSERT_LT (vban::seconds_since_epoch () - info.modified, 10);
	// Genesis block should be confirmed by default
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, vban::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, genesis.hash ());
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	vban::system system (2);
	for (auto & i : system.nodes)
	{
		auto transaction (i->store.tx_begin_read ());
		ASSERT_EQ (vban::genesis_amount, i->ledger.account_balance (transaction, vban::genesis_account));
	}
}

TEST (ledger, process_modifies_sideband)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	store->initialize (store->tx_begin_write (), genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (store->tx_begin_write (), send1).code);
	ASSERT_EQ (send1.sideband ().timestamp, store->block_get (store->tx_begin_read (), send1.hash ())->sideband ().timestamp);
}

// Create a send block and publish it.
TEST (ledger, process_send)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::keypair key2;
	vban::send_block send (info1.head, key2.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	vban::block_hash hash1 (send.hash ());
	ASSERT_EQ (vban::dev_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_EQ (1, info1.block_count);
	// This was a valid block, it should progress.
	auto return1 (ledger.process (transaction, send));
	ASSERT_EQ (vban::dev_genesis_key.pub, send.sideband ().account);
	ASSERT_EQ (2, send.sideband ().height);
	ASSERT_EQ (vban::genesis_amount - 50, ledger.amount (transaction, hash1));
	ASSERT_TRUE (store->frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (vban::dev_genesis_key.pub, store->frontier_get (transaction, hash1));
	ASSERT_EQ (vban::process_result::progress, return1.code);
	ASSERT_EQ (vban::dev_genesis_key.pub, store->block_account_calculated (send));
	ASSERT_EQ (50, ledger.account_balance (transaction, vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	vban::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info2));
	ASSERT_EQ (2, info2.block_count);
	auto latest6 (store->block_get (transaction, info2.head));
	ASSERT_NE (nullptr, latest6);
	auto latest7 (dynamic_cast<vban::send_block *> (latest6.get ()));
	ASSERT_NE (nullptr, latest7);
	ASSERT_EQ (send, *latest7);
	// Create an open block opening an account accepting the send we just created
	vban::open_block open (hash1, key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	vban::block_hash hash2 (open.hash ());
	// This was a valid block, it should progress.
	auto return2 (ledger.process (transaction, open));
	ASSERT_EQ (vban::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, open.sideband ().account);
	ASSERT_EQ (vban::genesis_amount - 50, open.sideband ().balance.number ());
	ASSERT_EQ (1, open.sideband ().height);
	ASSERT_EQ (vban::genesis_amount - 50, ledger.amount (transaction, hash2));
	ASSERT_EQ (vban::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, store->block_account_calculated (open));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.amount (transaction, hash2));
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.weight (key2.pub));
	vban::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info3));
	auto latest2 (store->block_get (transaction, info3.head));
	ASSERT_NE (nullptr, latest2);
	auto latest3 (dynamic_cast<vban::send_block *> (latest2.get ()));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (send, *latest3);
	vban::account_info info4;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info4));
	auto latest4 (store->block_get (transaction, info4.head));
	ASSERT_NE (nullptr, latest4);
	auto latest5 (dynamic_cast<vban::open_block *> (latest4.get ()));
	ASSERT_NE (nullptr, latest5);
	ASSERT_EQ (open, *latest5);
	ASSERT_FALSE (ledger.rollback (transaction, hash2));
	ASSERT_TRUE (store->frontier_get (transaction, hash2).is_zero ());
	vban::account_info info5;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info5));
	vban::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, vban::pending_key (key2.pub, hash1), pending1));
	ASSERT_EQ (vban::dev_genesis_key.pub, pending1.source);
	ASSERT_EQ (vban::genesis_amount - 50, pending1.amount.number ());
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, vban::dev_genesis_key.pub));
	ASSERT_EQ (50, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	vban::account_info info6;
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::dev_genesis_key.pub, info6));
	ASSERT_EQ (hash1, info6.head);
	ASSERT_FALSE (ledger.rollback (transaction, info6.head));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::dev_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_TRUE (store->frontier_get (transaction, hash1).is_zero ());
	vban::account_info info7;
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::dev_genesis_key.pub, info7));
	ASSERT_EQ (1, info7.block_count);
	ASSERT_EQ (info1.head, info7.head);
	vban::pending_info pending2;
	ASSERT_TRUE (ledger.store.pending_get (transaction, vban::pending_key (key2.pub, hash1), pending2));
	ASSERT_EQ (vban::genesis_amount, ledger.account_balance (transaction, vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, process_receive)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::keypair key2;
	vban::send_block send (info1.head, key2.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	vban::block_hash hash1 (send.hash ());
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	vban::keypair key3;
	vban::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	vban::block_hash hash2 (open.hash ());
	auto return1 (ledger.process (transaction, open));
	ASSERT_EQ (vban::process_result::progress, return1.code);
	ASSERT_EQ (key2.pub, store->block_account_calculated (open));
	ASSERT_EQ (key2.pub, open.sideband ().account);
	ASSERT_EQ (vban::genesis_amount - 50, open.sideband ().balance.number ());
	ASSERT_EQ (1, open.sideband ().height);
	ASSERT_EQ (vban::genesis_amount - 50, ledger.amount (transaction, hash2));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.weight (key3.pub));
	vban::send_block send2 (hash1, key2.pub, 25, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (hash1));
	vban::block_hash hash3 (send2.hash ());
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	vban::receive_block receive (hash2, hash3, key2.prv, key2.pub, *pool.generate (hash2));
	auto hash4 (receive.hash ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	auto return2 (ledger.process (transaction, receive));
	ASSERT_EQ (key2.pub, receive.sideband ().account);
	ASSERT_EQ (vban::genesis_amount - 25, receive.sideband ().balance.number ());
	ASSERT_EQ (2, receive.sideband ().height);
	ASSERT_EQ (25, ledger.amount (transaction, hash4));
	ASSERT_TRUE (store->frontier_get (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash4));
	ASSERT_EQ (vban::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, store->block_account_calculated (receive));
	ASSERT_EQ (hash4, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (25, ledger.account_balance (transaction, vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (vban::genesis_amount - 25, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (vban::genesis_amount - 25, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash4));
	ASSERT_TRUE (store->block_successor (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store->frontier_get (transaction, hash2));
	ASSERT_TRUE (store->frontier_get (transaction, hash4).is_zero ());
	ASSERT_EQ (25, ledger.account_balance (transaction, vban::dev_genesis_key.pub));
	ASSERT_EQ (25, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.weight (key3.pub));
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	vban::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, vban::pending_key (key2.pub, hash3), pending1));
	ASSERT_EQ (vban::dev_genesis_key.pub, pending1.source);
	ASSERT_EQ (25, pending1.amount.number ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, rollback_receiver)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::keypair key2;
	vban::send_block send (info1.head, key2.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	vban::block_hash hash1 (send.hash ());
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	vban::keypair key3;
	vban::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	vban::block_hash hash2 (open.hash ());
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash1));
	ASSERT_EQ (vban::genesis_amount, ledger.account_balance (transaction, vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	vban::account_info info2;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info2));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
	vban::pending_info pending1;
	ASSERT_TRUE (ledger.store.pending_get (transaction, vban::pending_key (key2.pub, info2.head), pending1));
}

TEST (ledger, rollback_representation)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key5;
	vban::change_block change1 (genesis.hash (), key5.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change1).code);
	vban::keypair key3;
	vban::change_block change2 (change1.hash (), key3.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (change1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change2).code);
	vban::keypair key2;
	vban::send_block send1 (change2.hash (), key2.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (change2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::keypair key4;
	vban::open_block open (send1.hash (), key4.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open).code);
	vban::send_block send2 (send1.hash (), key2.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	vban::receive_block receive1 (open.hash (), send2.hash (), key2.prv, key2.pub, *pool.generate (open.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (1, ledger.weight (key3.pub));
	ASSERT_EQ (vban::genesis_amount - 1, ledger.weight (key4.pub));
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info1));
	ASSERT_EQ (key4.pub, info1.representative);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	vban::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, key2.pub, info2));
	ASSERT_EQ (key4.pub, info2.representative);
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.weight (key4.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (1, ledger.weight (key3.pub));
	ASSERT_EQ (0, ledger.weight (key4.pub));
	ledger.rollback (transaction, send1.hash ());
	ASSERT_EQ (vban::genesis_amount, ledger.weight (key3.pub));
	vban::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info3));
	ASSERT_EQ (key3.pub, info3.representative);
	ASSERT_FALSE (ledger.rollback (transaction, change2.hash ()));
	vban::account_info info4;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info4));
	ASSERT_EQ (key5.pub, info4.representative);
	ASSERT_EQ (vban::genesis_amount, ledger.weight (key5.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
}

TEST (ledger, receive_rollback)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send (genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	vban::receive_block receive (send.hash (), send.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
}

TEST (ledger, process_duplicate)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::keypair key2;
	vban::send_block send (info1.head, key2.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	vban::block_hash hash1 (send.hash ());
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (vban::process_result::old, ledger.process (transaction, send).code);
	vban::open_block open (hash1, 1, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (vban::process_result::old, ledger.process (transaction, open).code);
}

TEST (ledger, representative_genesis)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	auto latest (ledger.latest (transaction, vban::dev_genesis_key.pub));
	ASSERT_FALSE (latest.is_zero ());
	ASSERT_EQ (genesis.open->hash (), ledger.representative (transaction, latest));
}

TEST (ledger, weight)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
}

TEST (ledger, representative_change)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::keypair key2;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::change_block block (info1.head, key2.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (vban::dev_genesis_key.pub, store->frontier_get (transaction, info1.head));
	auto return1 (ledger.process (transaction, block));
	ASSERT_EQ (0, ledger.amount (transaction, block.hash ()));
	ASSERT_TRUE (store->frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (vban::dev_genesis_key.pub, store->frontier_get (transaction, block.hash ()));
	ASSERT_EQ (vban::process_result::progress, return1.code);
	ASSERT_EQ (vban::dev_genesis_key.pub, store->block_account_calculated (block));
	ASSERT_EQ (0, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (key2.pub));
	vban::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info2));
	ASSERT_EQ (block.hash (), info2.head);
	ASSERT_FALSE (ledger.rollback (transaction, info2.head));
	ASSERT_EQ (vban::dev_genesis_key.pub, store->frontier_get (transaction, info1.head));
	ASSERT_TRUE (store->frontier_get (transaction, block.hash ()).is_zero ());
	vban::account_info info3;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info3));
	ASSERT_EQ (info1.head, info3.head);
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key2.pub));
}

TEST (ledger, send_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::keypair key2;
	vban::keypair key3;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::send_block block (info1.head, key2.pub, 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block).code);
	vban::send_block block2 (info1.head, key3.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, receive_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::keypair key2;
	vban::keypair key3;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::send_block block (info1.head, key2.pub, 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block).code);
	vban::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
	vban::change_block block3 (block2.hash (), key3.pub, key2.prv, key2.pub, *pool.generate (block2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block3).code);
	vban::send_block block4 (block.hash (), key2.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block4).code);
	vban::receive_block block5 (block2.hash (), block4.hash (), key2.prv, key2.pub, *pool.generate (block2.hash ()));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, block5).code);
}

TEST (ledger, open_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::keypair key2;
	vban::keypair key3;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::send_block block (info1.head, key2.pub, 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block).code);
	vban::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
	vban::open_block block3 (block.hash (), key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, block3).code);
}

TEST (system, DISABLED_generate_send_existing)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::thread_runner runner (system.io_ctx, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	vban::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (vban::genesis_account, stake_preserver.pub, vban::genesis_amount / 3 * 2, true));
	vban::account_info info1;
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account_get (transaction, vban::dev_genesis_key.pub, info1));
	}
	std::vector<vban::account> accounts;
	accounts.push_back (vban::dev_genesis_key.pub);
	system.generate_send_existing (node1, accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction (node1.store.tx_begin_write ());
		auto open_block (std::make_shared<vban::open_block> (send_block->hash (), vban::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (vban::genesis_account));
	vban::account_info info2;
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account_get (transaction, vban::dev_genesis_key.pub, info2));
	}
	ASSERT_NE (info1.head, info2.head);
	system.deadline_set (15s);
	while (info2.block_count < info1.block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.account_get (transaction, vban::dev_genesis_key.pub, info2));
	}
	ASSERT_EQ (info1.block_count + 2, info2.block_count);
	ASSERT_EQ (info2.balance, vban::genesis_amount / 3);
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_NE (node1.ledger.amount (transaction, info2.head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, DISABLED_generate_send_new)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::thread_runner runner (system.io_ctx, node1.config.io_threads);
	system.wallet (0)->insert_adhoc (vban::dev_genesis_key.prv);
	{
		auto transaction (node1.store.tx_begin_read ());
		auto iterator1 (node1.store.accounts_begin (transaction));
		ASSERT_NE (node1.store.accounts_end (), iterator1);
		++iterator1;
		ASSERT_EQ (node1.store.accounts_end (), iterator1);
	}
	vban::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (vban::genesis_account, stake_preserver.pub, vban::genesis_amount / 3 * 2, true));
	{
		auto transaction (node1.store.tx_begin_write ());
		auto open_block (std::make_shared<vban::open_block> (send_block->hash (), vban::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		node1.work_generate_blocking (*open_block);
		ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (node1.balance (stake_preserver.pub), node1.balance (vban::genesis_account));
	std::vector<vban::account> accounts;
	accounts.push_back (vban::dev_genesis_key.pub);
	// This indirectly waits for online weight to stabilize, required to prevent intermittent failures
	ASSERT_TIMELY (5s, node1.wallets.reps ().voting > 0);
	system.generate_send_new (node1, accounts);
	vban::account new_account (0);
	{
		auto transaction (node1.wallets.tx_begin_read ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (iterator2->first != vban::dev_genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (), iterator2);
		if (iterator2->first != vban::dev_genesis_key.pub)
		{
			new_account = iterator2->first;
		}
		++iterator2;
		ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
		ASSERT_FALSE (new_account.is_zero ());
	}
	ASSERT_TIMELY (10s, node1.balance (new_account) != 0);
	system.stop ();
	runner.join ();
}

TEST (ledger, representation_changes)
{
	vban::keypair key1;
	vban::rep_weights rep_weights;
	ASSERT_EQ (0, rep_weights.representation_get (key1.pub));
	rep_weights.representation_put (key1.pub, 1);
	ASSERT_EQ (1, rep_weights.representation_get (key1.pub));
	rep_weights.representation_put (key1.pub, 2);
	ASSERT_EQ (2, rep_weights.representation_get (key1.pub));
}

TEST (ledger, representation)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	auto & rep_weights = ledger.cache.rep_weights;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (vban::genesis_amount, rep_weights.representation_get (vban::dev_genesis_key.pub));
	vban::keypair key2;
	vban::send_block block1 (genesis.hash (), key2.pub, vban::genesis_amount - 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (vban::genesis_amount - 100, rep_weights.representation_get (vban::dev_genesis_key.pub));
	vban::keypair key3;
	vban::open_block block2 (block1.hash (), key3.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (vban::genesis_amount - 100, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key3.pub));
	vban::send_block block3 (block1.hash (), key2.pub, vban::genesis_amount - 200, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (vban::genesis_amount - 200, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key3.pub));
	vban::receive_block block4 (block2.hash (), block3.hash (), key2.prv, key2.pub, *pool.generate (block2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (vban::genesis_amount - 200, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key3.pub));
	vban::keypair key4;
	vban::change_block block5 (block4.hash (), key4.pub, key2.prv, key2.pub, *pool.generate (block4.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (vban::genesis_amount - 200, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key4.pub));
	vban::keypair key5;
	vban::send_block block6 (block5.hash (), key5.pub, 100, key2.prv, key2.pub, *pool.generate (block5.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (vban::genesis_amount - 200, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	vban::keypair key6;
	vban::open_block block7 (block6.hash (), key6.pub, key5.pub, key5.prv, key5.pub, *pool.generate (key5.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block7).code);
	ASSERT_EQ (vban::genesis_amount - 200, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key6.pub));
	vban::send_block block8 (block6.hash (), key5.pub, 0, key2.prv, key2.pub, *pool.generate (block6.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block8).code);
	ASSERT_EQ (vban::genesis_amount - 200, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (100, rep_weights.representation_get (key6.pub));
	vban::receive_block block9 (block7.hash (), block8.hash (), key5.prv, key5.pub, *pool.generate (block7.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block9).code);
	ASSERT_EQ (vban::genesis_amount - 200, rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key2.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key3.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key4.pub));
	ASSERT_EQ (0, rep_weights.representation_get (key5.pub));
	ASSERT_EQ (200, rep_weights.representation_get (key6.pub));
}

TEST (ledger, double_open)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key2;
	vban::send_block send1 (genesis.hash (), key2.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	vban::open_block open2 (send1.hash (), vban::dev_genesis_key.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, open2).code);
}

TEST (ledger, double_receive)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key2;
	vban::send_block send1 (genesis.hash (), key2.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	vban::receive_block receive1 (open1.hash (), send1.hash (), key2.prv, key2.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (vban::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (votes, check_signature)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.online_weight_minimum = vban::uint256_t ("50000000000000000000000000000000000000");
	auto & node1 = *system.add_node (node_config);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, vban::genesis_amount - 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *send1).code);
	}
	node1.scheduler.activate (vban::dev_genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_EQ (1, election1->votes ().size ());
	auto vote1 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 1, send1));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (vban::vote_code::invalid, node1.vote_processor.vote_blocking (vote1, std::make_shared<vban::transport::channel_loopback> (node1)));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (vban::vote_code::vote, node1.vote_processor.vote_blocking (vote1, std::make_shared<vban::transport::channel_loopback> (node1)));
	ASSERT_EQ (vban::vote_code::replay, node1.vote_processor.vote_blocking (vote1, std::make_shared<vban::transport::channel_loopback> (node1)));
}

TEST (votes, add_one)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, vban::genesis_amount - 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.block_confirm (send1);
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_EQ (1, election1->votes ().size ());
	auto vote1 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 1, send1));
	ASSERT_EQ (vban::vote_code::vote, node1.active.vote (vote1));
	auto vote2 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 2, send1));
	ASSERT_EQ (vban::vote_code::vote, node1.active.vote (vote2));
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes1 (election1->votes ());
	auto existing1 (votes1.find (vban::dev_genesis_key.pub));
	ASSERT_NE (votes1.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	vban::lock_guard<vban::mutex> guard (node1.active.mutex);
	auto winner (*election1->tally ().begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (vban::genesis_amount - 100, winner.first);
}

TEST (votes, add_two)
{
	vban::system system{ 1 };
	auto & node1 = *system.nodes[0];
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 = std::make_shared<vban::send_block> (genesis.hash (), key1.pub, vban::genesis_amount - 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0);
	node1.work_generate_blocking (*send1);
	auto transaction = node1.store.tx_begin_write ();
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.block_confirm (send1);
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	vban::keypair key2;
	auto send2 = std::make_shared<vban::send_block> (genesis.hash (), key2.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0);
	auto vote2 = std::make_shared<vban::vote> (key2.pub, key2.prv, 1, send2);
	ASSERT_EQ (vban::vote_code::vote, node1.active.vote (vote2));
	auto vote1 = std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 1, send1);
	ASSERT_EQ (vban::vote_code::vote, node1.active.vote (vote1));
	ASSERT_EQ (3, election1->votes ().size ());
	auto votes1 = election1->votes ();
	ASSERT_NE (votes1.end (), votes1.find (vban::dev_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1[vban::dev_genesis_key.pub].hash);
	ASSERT_NE (votes1.end (), votes1.find (key2.pub));
	ASSERT_EQ (send2->hash (), votes1[key2.pub].hash);
	ASSERT_EQ (*send1, *election1->winner ());
}

namespace vban
{
// Higher timestamps change the vote
TEST (votes, add_existing)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.online_weight_minimum = vban::genesis_amount;
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node1 = *system.add_node (node_config);
	vban::keypair key1;
	vban::block_builder builder;
	std::shared_ptr<vban::block> send1 = builder.state ()
										 .account (vban::dev_genesis_key.pub)
										 .previous (vban::genesis_hash)
										 .representative (vban::dev_genesis_key.pub) // No representative, blocks can't confirm
										 .balance (vban::genesis_amount / 2 - vban::Gxrb_ratio)
										 .link (key1.pub)
										 .work (0)
										 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
										 .build ();
	node1.work_generate_blocking (*send1);
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (node1.store.tx_begin_write (), *send1).code);
	node1.scheduler.activate (vban::dev_genesis_key.pub, node1.store.tx_begin_read ());
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	auto vote1 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 1, send1));
	ASSERT_EQ (vban::vote_code::vote, node1.active.vote (vote1));
	// Block is already processed from vote
	ASSERT_TRUE (node1.active.publish (send1));
	ASSERT_EQ (1, election1->last_votes[vban::dev_genesis_key.pub].timestamp);
	vban::keypair key2;
	std::shared_ptr<vban::block> send2 = builder.state ()
										 .account (vban::dev_genesis_key.pub)
										 .previous (vban::genesis_hash)
										 .representative (vban::dev_genesis_key.pub) // No representative, blocks can't confirm
										 .balance (vban::genesis_amount / 2 - vban::Gxrb_ratio)
										 .link (key2.pub)
										 .work (0)
										 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
										 .build ();
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 2, send2));
	// Pretend we've waited the timeout
	vban::unique_lock<vban::mutex> lock (election1->mutex);
	election1->last_votes[vban::dev_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_EQ (vban::vote_code::vote, node1.active.vote (vote2));
	ASSERT_FALSE (node1.active.publish (send2));
	ASSERT_EQ (2, election1->last_votes[vban::dev_genesis_key.pub].timestamp);
	// Also resend the old vote, and see if we respect the timestamp
	lock.lock ();
	election1->last_votes[vban::dev_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_EQ (vban::vote_code::replay, node1.active.vote (vote1));
	ASSERT_EQ (2, election1->votes ()[vban::dev_genesis_key.pub].timestamp);
	auto votes (election1->votes ());
	ASSERT_EQ (2, votes.size ());
	ASSERT_NE (votes.end (), votes.find (vban::dev_genesis_key.pub));
	ASSERT_EQ (send2->hash (), votes[vban::dev_genesis_key.pub].hash);
	ASSERT_EQ (*send2, *election1->tally ().begin ()->second);
}

// Lower timestamps are ignored
TEST (votes, add_old)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.block_confirm (send1);
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	auto vote1 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 2, send1));
	auto channel (std::make_shared<vban::transport::channel_loopback> (node1));
	node1.vote_processor.vote_blocking (vote1, channel);
	vban::keypair key2;
	auto send2 (std::make_shared<vban::send_block> (genesis.hash (), key2.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 1, send2));
	{
		vban::lock_guard<vban::mutex> lock (election1->mutex);
		election1->last_votes[vban::dev_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	}
	node1.vote_processor.vote_blocking (vote2, channel);
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes (election1->votes ());
	ASSERT_NE (votes.end (), votes.find (vban::dev_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes[vban::dev_genesis_key.pub].hash);
	ASSERT_EQ (*send1, *election1->winner ());
}
}

// Lower timestamps are accepted for different accounts
TEST (votes, add_old_different_account)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<vban::send_block> (send1->hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send2).code);
	vban::blocks_confirm (node1, { send1, send2 });
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election1);
	auto election2 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election2);
	ASSERT_EQ (1, election1->votes ().size ());
	ASSERT_EQ (1, election2->votes ().size ());
	auto vote1 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 2, send1));
	auto channel (std::make_shared<vban::transport::channel_loopback> (node1));
	auto vote_result1 (node1.vote_processor.vote_blocking (vote1, channel));
	ASSERT_EQ (vban::vote_code::vote, vote_result1);
	ASSERT_EQ (2, election1->votes ().size ());
	ASSERT_EQ (1, election2->votes ().size ());
	auto vote2 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 1, send2));
	auto vote_result2 (node1.vote_processor.vote_blocking (vote2, channel));
	ASSERT_EQ (vban::vote_code::vote, vote_result2);
	ASSERT_EQ (2, election1->votes ().size ());
	ASSERT_EQ (2, election2->votes ().size ());
	auto votes1 (election1->votes ());
	auto votes2 (election2->votes ());
	ASSERT_NE (votes1.end (), votes1.find (vban::dev_genesis_key.pub));
	ASSERT_NE (votes2.end (), votes2.find (vban::dev_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1[vban::dev_genesis_key.pub].hash);
	ASSERT_EQ (send2->hash (), votes2[vban::dev_genesis_key.pub].hash);
	ASSERT_EQ (*send1, *election1->winner ());
	ASSERT_EQ (*send2, *election2->winner ());
}

// The voting cooldown is respected
TEST (votes, add_cooldown)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	auto send1 (std::make_shared<vban::send_block> (genesis.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.block_confirm (send1);
	node1.scheduler.flush ();
	auto election1 = node1.active.election (send1->qualified_root ());
	auto vote1 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 1, send1));
	auto channel (std::make_shared<vban::transport::channel_loopback> (node1));
	node1.vote_processor.vote_blocking (vote1, channel);
	vban::keypair key2;
	auto send2 (std::make_shared<vban::send_block> (genesis.hash (), key2.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<vban::vote> (vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, 2, send2));
	node1.vote_processor.vote_blocking (vote2, channel);
	ASSERT_EQ (2, election1->votes ().size ());
	auto votes (election1->votes ());
	ASSERT_NE (votes.end (), votes.find (vban::dev_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes[vban::dev_genesis_key.pub].hash);
	ASSERT_EQ (*send1, *election1->winner ());
}

// Query for block successor
TEST (ledger, successor)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::keypair key1;
	vban::genesis genesis;
	vban::send_block send1 (genesis.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0);
	node1.work_generate_blocking (send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, send1).code);
	ASSERT_EQ (send1, *node1.ledger.successor (transaction, vban::qualified_root (vban::root (0), genesis.hash ())));
	ASSERT_EQ (*genesis.open, *node1.ledger.successor (transaction, genesis.open->qualified_root ()));
	ASSERT_EQ (nullptr, node1.ledger.successor (transaction, vban::qualified_root (0)));
}

TEST (ledger, fail_change_old)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::change_block block (genesis.hash (), key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::old, result2.code);
}

TEST (ledger, fail_change_gap_previous)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::change_block block (1, key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (vban::root (1)));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_change_bad_signature)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::change_block block (genesis.hash (), key1.pub, vban::keypair ().prv, 0, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_change_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::change_block block1 (genesis.hash (), key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::keypair key2;
	vban::change_block block2 (genesis.hash (), key2.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::fork, result2.code);
}

TEST (ledger, fail_send_old)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::old, result2.code);
}

TEST (ledger, fail_send_gap_previous)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block (1, key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (vban::root (1)));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_send_bad_signature)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block (genesis.hash (), key1.pub, 1, vban::keypair ().prv, 0, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (vban::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_send_negative_spend)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	vban::keypair key2;
	vban::send_block block2 (block1.hash (), key2.pub, 2, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (vban::process_result::negative_spend, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_send_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	vban::keypair key2;
	vban::send_block block2 (genesis.hash (), key2.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_old)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	vban::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (vban::process_result::old, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_gap_source)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::open_block block2 (1, 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::gap_source, result2.code);
}

TEST (ledger, fail_open_bad_signature)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	vban::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	block2.signature.clear ();
	ASSERT_EQ (vban::process_result::bad_signature, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_fork_previous)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	vban::send_block block2 (block1.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
	vban::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block3).code);
	vban::open_block block4 (block2.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, block4).code);
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, fail_open_account_mismatch)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	vban::keypair badkey;
	vban::open_block block2 (block1.hash (), 1, badkey.pub, badkey.prv, badkey.pub, *pool.generate (badkey.pub));
	ASSERT_NE (vban::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, fail_receive_old)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	vban::send_block block2 (block1.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
	vban::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block3).code);
	vban::receive_block block4 (block3.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block3.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (vban::process_result::old, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_receive_gap_source)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::send_block block2 (block1.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::progress, result2.code);
	vban::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (vban::process_result::progress, result3.code);
	vban::receive_block block4 (block3.hash (), 1, key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (vban::process_result::gap_source, result4.code);
}

TEST (ledger, fail_receive_overreceive)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::progress, result3.code);
	vban::receive_block block3 (block2.hash (), block1.hash (), key1.prv, key1.pub, *pool.generate (block2.hash ()));
	auto result4 (ledger.process (transaction, block3));
	ASSERT_EQ (vban::process_result::unreceivable, result4.code);
}

TEST (ledger, fail_receive_bad_signature)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::send_block block2 (block1.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::progress, result2.code);
	vban::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (vban::process_result::progress, result3.code);
	vban::receive_block block4 (block3.hash (), block2.hash (), vban::keypair ().prv, 0, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (vban::process_result::bad_signature, result4.code);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::send_block block2 (block1.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::progress, result2.code);
	vban::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (vban::process_result::progress, result3.code);
	vban::receive_block block4 (1, block2.hash (), key1.prv, key1.pub, *pool.generate (vban::root (1)));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (vban::process_result::gap_previous, result4.code);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::send_block block2 (block1.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::progress, result2.code);
	vban::receive_block block3 (1, block2.hash (), key1.prv, key1.pub, *pool.generate (vban::root (1)));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (vban::process_result::gap_previous, result3.code);
}

TEST (ledger, fail_receive_fork_previous)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::send_block block2 (block1.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::progress, result2.code);
	vban::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (vban::process_result::progress, result3.code);
	vban::keypair key2;
	vban::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (vban::process_result::progress, result4.code);
	vban::receive_block block5 (block3.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (vban::process_result::fork, result5.code);
}

TEST (ledger, fail_receive_received_source)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	vban::send_block block1 (genesis.hash (), key1.pub, 2, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (vban::process_result::progress, result1.code);
	vban::send_block block2 (block1.hash (), key1.pub, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (vban::process_result::progress, result2.code);
	vban::send_block block6 (block2.hash (), key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block2.hash ()));
	auto result6 (ledger.process (transaction, block6));
	ASSERT_EQ (vban::process_result::progress, result6.code);
	vban::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (vban::process_result::progress, result3.code);
	vban::keypair key2;
	vban::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (vban::process_result::progress, result4.code);
	vban::receive_block block5 (block4.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block4.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (vban::process_result::progress, result5.code);
	vban::receive_block block7 (block3.hash (), block2.hash (), key1.prv, key1.pub, *pool.generate (block3.hash ()));
	auto result7 (ledger.process (transaction, block7));
	ASSERT_EQ (vban::process_result::fork, result7.code);
}

TEST (ledger, latest_empty)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::keypair key;
	auto transaction (store->tx_begin_read ());
	auto latest (ledger.latest (transaction, key.pub));
	ASSERT_TRUE (latest.is_zero ());
}

TEST (ledger, latest_root)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key;
	ASSERT_EQ (key.pub, ledger.latest_root (transaction, key.pub));
	auto hash1 (ledger.latest (transaction, vban::dev_genesis_key.pub));
	vban::send_block send (hash1, 0, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (hash1));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (send.hash (), ledger.latest_root (transaction, vban::dev_genesis_key.pub));
}

TEST (ledger, change_representative_move_representation)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::keypair key1;
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	auto hash1 (genesis.hash ());
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::dev_genesis_key.pub));
	vban::send_block send (hash1, key1.pub, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (hash1));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (0, ledger.weight (vban::dev_genesis_key.pub));
	vban::keypair key2;
	vban::change_block change (send.hash (), key2.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change).code);
	vban::keypair key3;
	vban::open_block open (send.hash (), key3.pub, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (vban::genesis_amount, ledger.weight (key3.pub));
}

TEST (ledger, send_open_receive_rollback)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::keypair key1;
	vban::send_block send1 (info1.head, key1.pub, vban::genesis_amount - 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	auto return1 (ledger.process (transaction, send1));
	ASSERT_EQ (vban::process_result::progress, return1.code);
	vban::send_block send2 (send1.hash (), key1.pub, vban::genesis_amount - 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	auto return2 (ledger.process (transaction, send2));
	ASSERT_EQ (vban::process_result::progress, return2.code);
	vban::keypair key2;
	vban::open_block open (send2.hash (), key2.pub, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	auto return4 (ledger.process (transaction, open));
	ASSERT_EQ (vban::process_result::progress, return4.code);
	vban::receive_block receive (open.hash (), send1.hash (), key1.prv, key1.pub, *pool.generate (open.hash ()));
	auto return5 (ledger.process (transaction, receive));
	ASSERT_EQ (vban::process_result::progress, return5.code);
	vban::keypair key3;
	ASSERT_EQ (100, ledger.weight (key2.pub));
	ASSERT_EQ (vban::genesis_amount - 100, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	vban::change_block change1 (send2.hash (), key3.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send2.hash ()));
	auto return6 (ledger.process (transaction, change1));
	ASSERT_EQ (vban::process_result::progress, return6.code);
	ASSERT_EQ (100, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
	ASSERT_EQ (50, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount - 100, ledger.weight (key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (vban::genesis_amount - 100, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send2.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (vban::genesis_amount - 50, ledger.weight (vban::dev_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (key2.pub));
	ASSERT_EQ (0, ledger.weight (key3.pub));
	ASSERT_EQ (vban::genesis_amount - 0, ledger.weight (vban::dev_genesis_key.pub));
}

TEST (ledger, bootstrap_rep_weight)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::account_info info1;
	vban::keypair key2;
	vban::genesis genesis;
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	{
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger.cache);
		ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
		vban::send_block send (info1.head, key2.pub, vban::uint256_t ("50000000000000000000000000000000000000") - 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	}
	ASSERT_EQ (2, ledger.cache.block_count);
	{
		ledger.bootstrap_weight_max_blocks = 3;
		ledger.bootstrap_weights[key2.pub] = 1000;
		ASSERT_EQ (1000, ledger.weight (key2.pub));
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
		vban::send_block send (info1.head, key2.pub, vban::uint256_t ("50000000000000000000000000000000000000") - 100, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	}
	ASSERT_EQ (3, ledger.cache.block_count);
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (0, ledger.weight (key2.pub));
	}
}

TEST (ledger, block_destination_source)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair dest;
	vban::uint256_t balance (vban::genesis_amount);
	balance -= vban::Gxrb_ratio;
	vban::send_block block1 (genesis.hash (), dest.pub, balance, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	balance -= vban::Gxrb_ratio;
	vban::send_block block2 (block1.hash (), vban::genesis_account, balance, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	balance += vban::Gxrb_ratio;
	vban::receive_block block3 (block2.hash (), block2.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block2.hash ()));
	balance -= vban::Gxrb_ratio;
	vban::state_block block4 (vban::genesis_account, block3.hash (), vban::genesis_account, balance, dest.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block3.hash ()));
	balance -= vban::Gxrb_ratio;
	vban::state_block block5 (vban::genesis_account, block4.hash (), vban::genesis_account, balance, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block4.hash ()));
	balance += vban::Gxrb_ratio;
	vban::state_block block6 (vban::genesis_account, block5.hash (), vban::genesis_account, balance, block5.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block5.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (balance, ledger.balance (transaction, block6.hash ()));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block1));
	ASSERT_TRUE (ledger.block_source (transaction, block1).is_zero ());
	ASSERT_EQ (vban::genesis_account, ledger.block_destination (transaction, block2));
	ASSERT_TRUE (ledger.block_source (transaction, block2).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block3).is_zero ());
	ASSERT_EQ (block2.hash (), ledger.block_source (transaction, block3));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block4));
	ASSERT_TRUE (ledger.block_source (transaction, block4).is_zero ());
	ASSERT_EQ (vban::genesis_account, ledger.block_destination (transaction, block5));
	ASSERT_TRUE (ledger.block_source (transaction, block5).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block6).is_zero ());
	ASSERT_EQ (block5.hash (), ledger.block_source (transaction, block6));
}

TEST (ledger, state_account)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_EQ (vban::genesis_account, ledger.account (transaction, send1.hash ()));
}

TEST (ledger, state_send_receive)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_EQ (2, send2->sideband ().height);
	ASSERT_TRUE (send2->sideband ().details.is_send);
	ASSERT_FALSE (send2->sideband ().details.is_receive);
	ASSERT_FALSE (send2->sideband ().details.is_epoch);
	vban::state_block receive1 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (vban::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->sideband ().details.is_send);
	ASSERT_TRUE (receive2->sideband ().details.is_receive);
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_receive)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send1 (genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	vban::state_block receive1 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (vban::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->sideband ().details.is_send);
	ASSERT_TRUE (receive2->sideband ().details.is_receive);
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_rep_change)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair rep;
	vban::state_block change1 (vban::genesis_account, genesis.hash (), rep.pub, vban::genesis_amount, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (store->block_exists (transaction, change1.hash ()));
	auto change2 (store->block_get (transaction, change1.hash ()));
	ASSERT_NE (nullptr, change2);
	ASSERT_EQ (change1, *change2);
	ASSERT_EQ (vban::genesis_amount, ledger.balance (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.amount (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (rep.pub));
	ASSERT_EQ (2, change2->sideband ().height);
	ASSERT_FALSE (change2->sideband ().details.is_send);
	ASSERT_FALSE (change2->sideband ().details.is_receive);
	ASSERT_FALSE (change2->sideband ().details.is_epoch);
}

TEST (ledger, state_open)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (destination.pub, send1.hash ())));
	vban::state_block open1 (destination.pub, 0, vban::genesis_account, vban::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (destination.pub, send1.hash ())));
	ASSERT_TRUE (store->block_exists (transaction, open1.hash ()));
	auto open2 (store->block_get (transaction, open1.hash ()));
	ASSERT_NE (nullptr, open2);
	ASSERT_EQ (open1, *open2);
	ASSERT_EQ (vban::Gxrb_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
	ASSERT_EQ (ledger.cache.account_count, store->account_count (transaction));
	ASSERT_EQ (1, open2->sideband ().height);
	ASSERT_FALSE (open2->sideband ().details.is_send);
	ASSERT_TRUE (open2->sideband ().details.is_receive);
	ASSERT_FALSE (open2->sideband ().details.is_epoch);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, send_after_state_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::send_block send2 (send1.hash (), vban::genesis_account, vban::genesis_amount - (2 * vban::Gxrb_ratio), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, send2).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, receive_after_state_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::receive_block receive1 (send1.hash (), send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, receive1).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, change_after_state_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::keypair rep;
	vban::change_block change1 (send1.hash (), rep.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, change1).code);
}

TEST (ledger, state_unreceivable_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send1 (genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	vban::state_block receive1 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount, 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::gap_source, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_receive_bad_amount_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send1 (genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	vban::state_block receive1 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::balance_mismatch, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_no_link_amount_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::keypair rep;
	vban::state_block change1 (vban::genesis_account, send1.hash (), rep.pub, vban::genesis_amount, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::balance_mismatch, ledger.process (transaction, change1).code);
}

TEST (ledger, state_receive_wrong_account_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	vban::keypair key;
	vban::state_block receive1 (key.pub, 0, vban::genesis_account, vban::Gxrb_ratio, send1.hash (), key.prv, key.pub, *pool.generate (key.pub));
	ASSERT_EQ (vban::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_open_state_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block open1 (destination.pub, 0, vban::genesis_account, vban::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	vban::open_block open2 (send1.hash (), vban::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_state_open_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::open_block open1 (send1.hash (), vban::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	vban::state_block open2 (destination.pub, 0, vban::genesis_account, vban::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_open_previous_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block open1 (destination.pub, 1, vban::genesis_account, vban::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (1));
	ASSERT_EQ (vban::process_result::gap_previous, ledger.process (transaction, open1).code);
}

TEST (ledger, state_open_source_fail)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block open1 (destination.pub, 0, vban::genesis_account, 0, 0, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::gap_source, ledger.process (transaction, open1).code);
}

TEST (ledger, state_send_change)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair rep;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), rep.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (rep.pub));
	ASSERT_EQ (2, send2->sideband ().height);
	ASSERT_TRUE (send2->sideband ().details.is_send);
	ASSERT_FALSE (send2->sideband ().details.is_receive);
	ASSERT_FALSE (send2->sideband ().details.is_epoch);
}

TEST (ledger, state_receive_change)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	vban::keypair rep;
	vban::state_block receive1 (vban::genesis_account, send1.hash (), rep.pub, vban::genesis_amount, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive2 (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (vban::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (0, ledger.weight (vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (rep.pub));
	ASSERT_EQ (3, receive2->sideband ().height);
	ASSERT_FALSE (receive2->sideband ().details.is_send);
	ASSERT_TRUE (receive2->sideband ().details.is_receive);
	ASSERT_FALSE (receive2->sideband ().details.is_epoch);
}

TEST (ledger, state_open_old)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::open_block open1 (send1.hash (), vban::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_EQ (vban::Gxrb_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
}

TEST (ledger, state_receive_old)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block send2 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - (2 * vban::Gxrb_ratio), destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	vban::open_block open1 (send1.hash (), vban::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	vban::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (2 * vban::Gxrb_ratio, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
}

TEST (ledger, state_rollback_send)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send2 (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	vban::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (vban::genesis_account, send1.hash ()), info));
	ASSERT_EQ (vban::genesis_account, info.source);
	ASSERT_EQ (vban::Gxrb_ratio, info.amount.number ());
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_TRUE (store->block_successor (transaction, genesis.hash ()).is_zero ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_rollback_receive)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block receive1 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	vban::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (vban::genesis_account, send1.hash ()), info));
	ASSERT_EQ (vban::genesis_account, info.source);
	ASSERT_EQ (vban::Gxrb_ratio, info.amount.number ());
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_rollback_received_send)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block receive1 (key.pub, 0, key.pub, vban::Gxrb_ratio, send1.hash (), key.prv, key.pub, *pool.generate (key.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
	ASSERT_EQ (0, ledger.account_balance (transaction, key.pub));
	ASSERT_EQ (0, ledger.weight (key.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_rep_change_rollback)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair rep;
	vban::state_block change1 (vban::genesis_account, genesis.hash (), rep.pub, vban::genesis_amount, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, change1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
	ASSERT_EQ (0, ledger.weight (rep.pub));
}

TEST (ledger, state_open_rollback)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block open1 (destination.pub, 0, vban::genesis_account, vban::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (ledger.rollback (transaction, open1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, open1.hash ()));
	ASSERT_EQ (0, ledger.account_balance (transaction, destination.pub));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	vban::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (destination.pub, send1.hash ()), info));
	ASSERT_EQ (vban::genesis_account, info.source);
	ASSERT_EQ (vban::Gxrb_ratio, info.amount.number ());
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_send_change_rollback)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair rep;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), rep.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_EQ (vban::genesis_amount, ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount, ledger.weight (vban::genesis_account));
	ASSERT_EQ (0, ledger.weight (rep.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, state_receive_change_rollback)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::keypair rep;
	vban::state_block receive1 (vban::genesis_account, send1.hash (), rep.pub, vban::genesis_amount, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.account_balance (transaction, vban::genesis_account));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	ASSERT_EQ (0, ledger.weight (rep.pub));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, epoch_blocks_v1_general)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block epoch1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (epoch1.sideband ().details.is_send);
	ASSERT_FALSE (epoch1.sideband ().details.is_receive);
	ASSERT_TRUE (epoch1.sideband ().details.is_epoch);
	ASSERT_EQ (vban::epoch::epoch_1, epoch1.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch1.sideband ().source_epoch); // Not used for epoch blocks
	vban::state_block epoch2 (vban::genesis_account, epoch1.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, epoch2).code);
	vban::account_info genesis_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), vban::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), vban::epoch::epoch_0);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), vban::epoch::epoch_1);
	ASSERT_FALSE (epoch1.sideband ().details.is_send);
	ASSERT_FALSE (epoch1.sideband ().details.is_receive);
	ASSERT_TRUE (epoch1.sideband ().details.is_epoch);
	ASSERT_EQ (vban::epoch::epoch_1, epoch1.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch1.sideband ().source_epoch); // Not used for epoch blocks
	vban::change_block change1 (epoch1.hash (), vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, change1).code);
	vban::state_block send1 (vban::genesis_account, epoch1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (send1.sideband ().details.is_send);
	ASSERT_FALSE (send1.sideband ().details.is_receive);
	ASSERT_FALSE (send1.sideband ().details.is_epoch);
	ASSERT_EQ (vban::epoch::epoch_1, send1.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, send1.sideband ().source_epoch); // Not used for send blocks
	vban::open_block open1 (send1.hash (), vban::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::unreceivable, ledger.process (transaction, open1).code);
	vban::state_block epoch3 (destination.pub, 0, vban::genesis_account, 0, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::representative_mismatch, ledger.process (transaction, epoch3).code);
	vban::state_block epoch4 (destination.pub, 0, 0, 0, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch4).code);
	ASSERT_FALSE (epoch4.sideband ().details.is_send);
	ASSERT_FALSE (epoch4.sideband ().details.is_receive);
	ASSERT_TRUE (epoch4.sideband ().details.is_epoch);
	ASSERT_EQ (vban::epoch::epoch_1, epoch4.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch4.sideband ().source_epoch); // Not used for epoch blocks
	vban::receive_block receive1 (epoch4.hash (), send1.hash (), destination.prv, destination.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, receive1).code);
	vban::state_block receive2 (destination.pub, epoch4.hash (), destination.pub, vban::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (vban::epoch::epoch_1, receive2.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_1, receive2.sideband ().source_epoch);
	ASSERT_EQ (0, ledger.balance (transaction, epoch4.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.balance (transaction, receive2.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, receive2.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.weight (destination.pub));
	ASSERT_FALSE (receive2.sideband ().details.is_send);
	ASSERT_TRUE (receive2.sideband ().details.is_receive);
	ASSERT_FALSE (receive2.sideband ().details.is_epoch);
}

TEST (ledger, epoch_blocks_v2_general)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block epoch1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	// Trying to upgrade from epoch 0 to epoch 2. It is a requirement epoch upgrades are sequential unless the account is unopened
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, epoch1).code);
	// Set it to the first epoch and it should now succeed
	epoch1 = vban::state_block (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, epoch1.work);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_EQ (vban::epoch::epoch_1, epoch1.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch1.sideband ().source_epoch); // Not used for epoch blocks
	vban::state_block epoch2 (vban::genesis_account, epoch1.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch2).code);
	ASSERT_EQ (vban::epoch::epoch_2, epoch2.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch2.sideband ().source_epoch); // Not used for epoch blocks
	vban::state_block epoch3 (vban::genesis_account, epoch2.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch2.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, epoch3).code);
	vban::account_info genesis_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), vban::epoch::epoch_2);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), vban::epoch::epoch_0);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, vban::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch (), vban::epoch::epoch_1);
	vban::change_block change1 (epoch1.hash (), vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, change1).code);
	vban::state_block send1 (vban::genesis_account, epoch1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_EQ (vban::epoch::epoch_1, send1.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, send1.sideband ().source_epoch); // Not used for send blocks
	vban::open_block open1 (send1.hash (), vban::genesis_account, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::unreceivable, ledger.process (transaction, open1).code);
	vban::state_block epoch4 (destination.pub, 0, 0, 0, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch4).code);
	ASSERT_EQ (vban::epoch::epoch_1, epoch4.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch4.sideband ().source_epoch); // Not used for epoch blocks
	vban::state_block epoch5 (destination.pub, epoch4.hash (), vban::genesis_account, 0, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (vban::process_result::representative_mismatch, ledger.process (transaction, epoch5).code);
	vban::state_block epoch6 (destination.pub, epoch4.hash (), 0, 0, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch4.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch6).code);
	ASSERT_EQ (vban::epoch::epoch_2, epoch6.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch6.sideband ().source_epoch); // Not used for epoch blocks
	vban::receive_block receive1 (epoch6.hash (), send1.hash (), destination.prv, destination.pub, *pool.generate (epoch6.hash ()));
	ASSERT_EQ (vban::process_result::block_position, ledger.process (transaction, receive1).code);
	vban::state_block receive2 (destination.pub, epoch6.hash (), destination.pub, vban::Gxrb_ratio, send1.hash (), destination.prv, destination.pub, *pool.generate (epoch6.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (vban::epoch::epoch_2, receive2.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_1, receive2.sideband ().source_epoch);
	ASSERT_EQ (0, ledger.balance (transaction, epoch6.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.balance (transaction, receive2.hash ()));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.amount (transaction, receive2.hash ()));
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio, ledger.weight (vban::genesis_account));
	ASSERT_EQ (vban::Gxrb_ratio, ledger.weight (destination.pub));
}

TEST (ledger, epoch_blocks_receive_upgrade)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block epoch1 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch1).code);
	vban::state_block send2 (vban::genesis_account, epoch1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 2, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_EQ (vban::epoch::epoch_1, send2.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, send2.sideband ().source_epoch); // Not used for send blocks
	vban::open_block open1 (send1.hash (), destination.pub, destination.pub, destination.prv, destination.pub, *pool.generate (destination.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_EQ (vban::epoch::epoch_0, open1.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, open1.sideband ().source_epoch);
	vban::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (vban::process_result::unreceivable, ledger.process (transaction, receive1).code);
	vban::state_block receive2 (destination.pub, open1.hash (), destination.pub, vban::Gxrb_ratio * 2, send2.hash (), destination.prv, destination.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (vban::epoch::epoch_1, receive2.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_1, receive2.sideband ().source_epoch);
	vban::account_info destination_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), vban::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, receive2.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), vban::epoch::epoch_0);
	vban::pending_info pending_send2;
	ASSERT_FALSE (ledger.store.pending_get (transaction, vban::pending_key (destination.pub, send2.hash ()), pending_send2));
	ASSERT_EQ (vban::dev_genesis_key.pub, pending_send2.source);
	ASSERT_EQ (vban::Gxrb_ratio, pending_send2.amount.number ());
	ASSERT_EQ (vban::epoch::epoch_1, pending_send2.epoch);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (vban::epoch::epoch_1, receive2.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_1, receive2.sideband ().source_epoch);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), vban::epoch::epoch_1);
	vban::keypair destination2;
	vban::state_block send3 (destination.pub, receive2.hash (), destination.pub, vban::Gxrb_ratio, destination2.pub, destination.prv, destination.pub, *pool.generate (receive2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send3).code);
	vban::open_block open2 (send3.hash (), destination2.pub, destination2.pub, destination2.prv, destination2.pub, *pool.generate (destination2.pub));
	ASSERT_EQ (vban::process_result::unreceivable, ledger.process (transaction, open2).code);
	// Upgrade to epoch 2 and send to destination. Try to create an open block from an epoch 2 source block.
	vban::keypair destination3;
	vban::state_block epoch2 (vban::genesis_account, send2.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 2, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch2).code);
	vban::state_block send4 (vban::genesis_account, epoch2.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 3, destination3.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send4).code);
	vban::open_block open3 (send4.hash (), destination3.pub, destination3.pub, destination3.prv, destination3.pub, *pool.generate (destination3.pub));
	ASSERT_EQ (vban::process_result::unreceivable, ledger.process (transaction, open3).code);
	// Send it to an epoch 1 account
	vban::state_block send5 (vban::genesis_account, send4.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 4, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send4.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send5).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), vban::epoch::epoch_1);
	vban::state_block receive3 (destination.pub, send3.hash (), destination.pub, vban::Gxrb_ratio * 2, send5.hash (), destination.prv, destination.pub, *pool.generate (send3.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive3).code);
	ASSERT_EQ (vban::epoch::epoch_2, receive3.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_2, receive3.sideband ().source_epoch);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch (), vban::epoch::epoch_2);
	// Upgrade an unopened account straight to epoch 2
	vban::keypair destination4;
	vban::state_block send6 (vban::genesis_account, send5.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 5, destination4.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send5.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send6).code);
	vban::state_block epoch4 (destination4.pub, 0, 0, 0, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (destination4.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch4).code);
	ASSERT_EQ (vban::epoch::epoch_2, epoch4.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch4.sideband ().source_epoch); // Not used for epoch blocks
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
}

TEST (ledger, epoch_blocks_fork)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	vban::send_block send1 (genesis.hash (), vban::account (0), vban::genesis_amount, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	vban::state_block epoch1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, epoch1).code);
	vban::state_block epoch2 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, epoch2).code);
	vban::state_block epoch3 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch3).code);
	ASSERT_EQ (vban::epoch::epoch_1, epoch3.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch3.sideband ().source_epoch); // Not used for epoch state blocks
	vban::state_block epoch4 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::fork, ledger.process (transaction, epoch2).code);
}

TEST (ledger, successor_epoch)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::keypair key1;
	vban::genesis genesis;
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send1 (genesis.hash (), key1.pub, vban::genesis_amount - 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	vban::state_block open (key1.pub, 0, key1.pub, 1, send1.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	vban::state_block change (key1.pub, open.hash (), key1.pub, 1, 0, key1.prv, key1.pub, *pool.generate (open.hash ()));
	auto open_hash = open.hash ();
	vban::send_block send2 (send1.hash (), reinterpret_cast<vban::account const &> (open_hash), vban::genesis_amount - 2, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	vban::state_block epoch_open (reinterpret_cast<vban::account const &> (open_hash), 0, 0, 0, node1.ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (open.hash ()));
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, send1).code);
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, open).code);
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, change).code);
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, send2).code);
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (change, *node1.ledger.successor (transaction, change.qualified_root ()));
	ASSERT_EQ (epoch_open, *node1.ledger.successor (transaction, epoch_open.qualified_root ()));
	ASSERT_EQ (vban::epoch::epoch_1, epoch_open.sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, epoch_open.sideband ().source_epoch); // Not used for epoch state blocks
}

TEST (ledger, epoch_open_pending)
{
	vban::block_builder builder;
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	auto epoch_open = builder.state ()
					  .account (key1.pub)
					  .previous (0)
					  .representative (0)
					  .balance (0)
					  .link (node1.ledger.epoch_link (vban::epoch::epoch_1))
					  .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
					  .work (*pool.generate (key1.pub))
					  .build_shared ();
	auto process_result (node1.ledger.process (node1.store.tx_begin_write (), *epoch_open));
	ASSERT_EQ (vban::process_result::gap_epoch_open_pending, process_result.code);
	ASSERT_EQ (vban::signature_verification::valid_epoch, process_result.verified);
	node1.block_processor.add (epoch_open);
	node1.block_processor.flush ();
	ASSERT_FALSE (node1.ledger.block_or_pruned_exists (epoch_open->hash ()));
	// Open block should be inserted into unchecked
	auto blocks (node1.store.unchecked_get (node1.store.tx_begin_read (), vban::hash_or_account (epoch_open->account ()).hash));
	ASSERT_EQ (blocks.size (), 1);
	ASSERT_EQ (blocks[0].block->full_hash (), epoch_open->full_hash ());
	ASSERT_EQ (blocks[0].verified, vban::signature_verification::valid_epoch);
	// New block to process epoch open
	auto send1 = builder.state ()
				 .account (vban::genesis_account)
				 .previous (vban::genesis_hash)
				 .representative (vban::genesis_account)
				 .balance (vban::genesis_amount - 100)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*pool.generate (vban::genesis_hash))
				 .build_shared ();
	node1.block_processor.add (send1);
	node1.block_processor.flush ();
	ASSERT_TRUE (node1.ledger.block_or_pruned_exists (epoch_open->hash ()));
}

TEST (ledger, block_hash_account_conflict)
{
	vban::block_builder builder;
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair key1;
	vban::keypair key2;
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());

	/*
	 * Generate a send block whose destination is a block hash already
	 * in the ledger and not an account
	 */
	auto send1 = builder.state ()
				 .account (vban::genesis_account)
				 .previous (genesis.hash ())
				 .representative (vban::genesis_account)
				 .balance (vban::genesis_amount - 100)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*pool.generate (genesis.hash ()))
				 .build_shared ();

	auto receive1 = builder.state ()
					.account (key1.pub)
					.previous (0)
					.representative (vban::genesis_account)
					.balance (100)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (key1.pub))
					.build_shared ();

	/*
	 * Note that the below link is a block hash when this is intended
	 * to represent a send state block. This can generally never be
	 * received , except by epoch blocks, which can sign an open block
	 * for arbitrary accounts.
	 */
	auto send2 = builder.state ()
				 .account (key1.pub)
				 .previous (receive1->hash ())
				 .representative (vban::genesis_account)
				 .balance (90)
				 .link (receive1->hash ())
				 .sign (key1.prv, key1.pub)
				 .work (*pool.generate (receive1->hash ()))
				 .build_shared ();

	/*
	 * Generate an epoch open for the account with the same value as the block hash
	 */
	auto receive1_hash = receive1->hash ();
	auto open_epoch1 = builder.state ()
					   .account (reinterpret_cast<vban::account const &> (receive1_hash))
					   .previous (0)
					   .representative (0)
					   .balance (0)
					   .link (node1.ledger.epoch_link (vban::epoch::epoch_1))
					   .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
					   .work (*pool.generate (receive1->hash ()))
					   .build_shared ();

	node1.work_generate_blocking (*send1);
	node1.work_generate_blocking (*receive1);
	node1.work_generate_blocking (*send2);
	node1.work_generate_blocking (*open_epoch1);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send1).code);
	ASSERT_EQ (vban::process_result::progress, node1.process (*receive1).code);
	ASSERT_EQ (vban::process_result::progress, node1.process (*send2).code);
	ASSERT_EQ (vban::process_result::progress, node1.process (*open_epoch1).code);
	vban::blocks_confirm (node1, { send1, receive1, send2, open_epoch1 });
	auto election1 = node1.active.election (send1->qualified_root ());
	ASSERT_NE (nullptr, election1);
	auto election2 = node1.active.election (receive1->qualified_root ());
	ASSERT_NE (nullptr, election2);
	auto election3 = node1.active.election (send2->qualified_root ());
	ASSERT_NE (nullptr, election3);
	auto election4 = node1.active.election (open_epoch1->qualified_root ());
	ASSERT_NE (nullptr, election4);
	auto winner1 (election1->winner ());
	auto winner2 (election2->winner ());
	auto winner3 (election3->winner ());
	auto winner4 (election4->winner ());
	ASSERT_EQ (*send1, *winner1);
	ASSERT_EQ (*receive1, *winner2);
	ASSERT_EQ (*send2, *winner3);
	ASSERT_EQ (*open_epoch1, *winner4);
}

TEST (ledger, could_fit)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair destination;
	// Test legacy and state change blocks could_fit
	vban::change_block change1 (genesis.hash (), vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	vban::state_block change2 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	// Test legacy and state send
	vban::keypair key1;
	vban::send_block send1 (change1.hash (), key1.pub, vban::genesis_amount - 1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (change1.hash ()));
	vban::state_block send2 (vban::genesis_account, change1.hash (), vban::genesis_account, vban::genesis_amount - 1, key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (change1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, send1));
	ASSERT_FALSE (ledger.could_fit (transaction, send2));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	// Test legacy and state open
	vban::open_block open1 (send2.hash (), vban::genesis_account, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	vban::state_block open2 (key1.pub, 0, vban::genesis_account, 1, send2.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_FALSE (ledger.could_fit (transaction, open1));
	ASSERT_FALSE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	// Create another send to receive
	vban::state_block send3 (vban::genesis_account, send2.hash (), vban::genesis_account, vban::genesis_amount - 2, key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send2.hash ()));
	// Test legacy and state receive
	vban::receive_block receive1 (open1.hash (), send3.hash (), key1.prv, key1.pub, *pool.generate (open1.hash ()));
	vban::state_block receive2 (key1.pub, open1.hash (), vban::genesis_account, 2, send3.hash (), key1.prv, key1.pub, *pool.generate (open1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, receive1));
	ASSERT_FALSE (ledger.could_fit (transaction, receive2));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send3).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	// Test epoch (state)
	vban::state_block epoch1 (key1.pub, receive1.hash (), vban::genesis_account, 2, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (receive1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
}

TEST (ledger, unchecked_epoch)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair destination;
	auto send1 (std::make_shared<vban::state_block> (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<vban::state_block> (destination.pub, 0, destination.pub, vban::Gxrb_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto epoch1 (std::make_shared<vban::state_block> (destination.pub, open1->hash (), destination.pub, vban::Gxrb_ratio, node1.ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*epoch1);
	node1.block_processor.add (epoch1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, vban::signature_verification::valid_epoch);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
		vban::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_EQ (info.epoch (), vban::epoch::epoch_1);
	}
}

TEST (ledger, unchecked_epoch_invalid)
{
	vban::system system;
	vban::node_config node_config (vban::get_available_port (), system.logging);
	node_config.frontiers_confirmation = vban::frontiers_confirmation_mode::disabled;
	auto & node1 (*system.add_node (node_config));
	vban::genesis genesis;
	vban::keypair destination;
	auto send1 (std::make_shared<vban::state_block> (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<vban::state_block> (destination.pub, 0, destination.pub, vban::Gxrb_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Epoch block with account own signature
	auto epoch1 (std::make_shared<vban::state_block> (destination.pub, open1->hash (), destination.pub, vban::Gxrb_ratio, node1.ledger.epoch_link (vban::epoch::epoch_1), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch1);
	// Pseudo epoch block (send subtype, destination - epoch link)
	auto epoch2 (std::make_shared<vban::state_block> (destination.pub, open1->hash (), destination.pub, vban::Gxrb_ratio - 1, node1.ledger.epoch_link (vban::epoch::epoch_1), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch2);
	node1.block_processor.add (epoch1);
	node1.block_processor.add (epoch2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 2);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 2);
		ASSERT_EQ (blocks[0].verified, vban::signature_verification::valid);
		ASSERT_EQ (blocks[1].verified, vban::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.block_exists (transaction, epoch1->hash ()));
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch2->hash ()));
		ASSERT_TRUE (node1.active.empty ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
		vban::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_NE (info.epoch (), vban::epoch::epoch_1);
		auto epoch2_store (node1.store.block_get (transaction, epoch2->hash ()));
		ASSERT_NE (nullptr, epoch2_store);
		ASSERT_EQ (vban::epoch::epoch_0, epoch2_store->sideband ().details.epoch);
		ASSERT_TRUE (epoch2_store->sideband ().details.is_send);
		ASSERT_FALSE (epoch2_store->sideband ().details.is_epoch);
		ASSERT_FALSE (epoch2_store->sideband ().details.is_receive);
	}
}

TEST (ledger, unchecked_open)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair destination;
	auto send1 (std::make_shared<vban::state_block> (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<vban::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Invalid signature for open block
	auto open2 (std::make_shared<vban::open_block> (send1->hash (), vban::dev_genesis_key.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open2);
	open2->signature.bytes[0] ^= 1;
	node1.block_processor.add (open1);
	node1.block_processor.add (open2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
		auto blocks (node1.store.unchecked_get (transaction, open1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, vban::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, open1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
	}
}

TEST (ledger, unchecked_receive)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::keypair destination;
	auto send1 (std::make_shared<vban::state_block> (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<vban::state_block> (vban::genesis_account, send1->hash (), vban::genesis_account, vban::genesis_amount - 2 * vban::Gxrb_ratio, destination.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto open1 (std::make_shared<vban::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto receive1 (std::make_shared<vban::receive_block> (open1->hash (), send2->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*receive1);
	node1.block_processor.add (send1);
	node1.block_processor.add (receive1);
	node1.block_processor.flush ();
	// Previous block for receive1 is unknown, signature cannot be validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
		auto blocks (node1.store.unchecked_get (transaction, receive1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, vban::signature_verification::unknown);
	}
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	// Previous block for receive1 is known, signature was validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
		auto blocks (node1.store.unchecked_get (transaction, receive1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, vban::signature_verification::valid);
	}
	node1.block_processor.add (send2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, receive1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		ASSERT_EQ (unchecked_count, node1.store.unchecked_count (transaction));
	}
}

TEST (ledger, confirmation_height_not_updated)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::account_info account_info;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, account_info));
	vban::keypair key;
	vban::send_block send1 (account_info.head, key.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (account_info.head));
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, vban::genesis_account, confirmation_height_info));
	ASSERT_EQ (1, confirmation_height_info.height);
	ASSERT_EQ (genesis.hash (), confirmation_height_info.frontier);
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (store->confirmation_height_get (transaction, vban::genesis_account, confirmation_height_info));
	ASSERT_EQ (1, confirmation_height_info.height);
	ASSERT_EQ (genesis.hash (), confirmation_height_info.frontier);
	vban::open_block open1 (send1.hash (), vban::genesis_account, key.pub, key.prv, key.pub, *pool.generate (key.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_TRUE (store->confirmation_height_get (transaction, key.pub, confirmation_height_info));
	ASSERT_EQ (0, confirmation_height_info.height);
	ASSERT_EQ (vban::block_hash (0), confirmation_height_info.frontier);
}

TEST (ledger, zero_rep)
{
	vban::system system (1);
	auto & node1 (*system.nodes[0]);
	vban::genesis genesis;
	vban::block_builder builder;
	auto block1 = builder.state ()
				  .account (vban::dev_genesis_key.pub)
				  .previous (genesis.hash ())
				  .representative (0)
				  .balance (vban::genesis_amount)
				  .link (0)
				  .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				  .work (*system.work.generate (genesis.hash ()))
				  .build ();
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *block1).code);
	ASSERT_EQ (0, node1.ledger.cache.rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (vban::genesis_amount, node1.ledger.cache.rep_weights.representation_get (0));
	auto block2 = builder.state ()
				  .account (vban::dev_genesis_key.pub)
				  .previous (block1->hash ())
				  .representative (vban::dev_genesis_key.pub)
				  .balance (vban::genesis_amount)
				  .link (0)
				  .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				  .work (*system.work.generate (block1->hash ()))
				  .build ();
	ASSERT_EQ (vban::process_result::progress, node1.ledger.process (transaction, *block2).code);
	ASSERT_EQ (vban::genesis_amount, node1.ledger.cache.rep_weights.representation_get (vban::dev_genesis_key.pub));
	ASSERT_EQ (0, node1.ledger.cache.rep_weights.representation_get (0));
}

TEST (ledger, work_validation)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	store->initialize (store->tx_begin_write (), genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::block_builder builder;
	auto gen = vban::dev_genesis_key;
	vban::keypair key;

	// With random work the block doesn't pass, then modifies the block with sufficient work and ensures a correct result
	auto process_block = [&store, &ledger, &pool] (vban::block & block_a, vban::block_details const details_a) {
		auto threshold = vban::work_threshold (block_a.work_version (), details_a);
		// Rarely failed with random work, so modify until it doesn't have enough difficulty
		while (block_a.difficulty () >= threshold)
		{
			block_a.block_work_set (block_a.block_work () + 1);
		}
		EXPECT_EQ (vban::process_result::insufficient_work, ledger.process (store->tx_begin_write (), block_a).code);
		block_a.block_work_set (*pool.generate (block_a.root (), threshold));
		EXPECT_EQ (vban::process_result::progress, ledger.process (store->tx_begin_write (), block_a).code);
	};

	std::error_code ec;

	auto send = *builder.send ()
				 .previous (vban::genesis_hash)
				 .destination (gen.pub)
				 .balance (vban::genesis_amount - 1)
				 .sign (gen.prv, gen.pub)
				 .work (0)
				 .build (ec);
	ASSERT_FALSE (ec);

	auto receive = *builder.receive ()
					.previous (send.hash ())
					.source (send.hash ())
					.sign (gen.prv, gen.pub)
					.work (0)
					.build (ec);
	ASSERT_FALSE (ec);

	auto change = *builder.change ()
				   .previous (receive.hash ())
				   .representative (key.pub)
				   .sign (gen.prv, gen.pub)
				   .work (0)
				   .build (ec);
	ASSERT_FALSE (ec);

	auto state = *builder.state ()
				  .account (gen.pub)
				  .previous (change.hash ())
				  .representative (gen.pub)
				  .balance (vban::genesis_amount - 1)
				  .link (key.pub)
				  .sign (gen.prv, gen.pub)
				  .work (0)
				  .build (ec);
	ASSERT_FALSE (ec);

	auto open = *builder.open ()
				 .account (key.pub)
				 .source (state.hash ())
				 .representative (key.pub)
				 .sign (key.prv, key.pub)
				 .work (0)
				 .build (ec);
	ASSERT_FALSE (ec);

	auto epoch = *builder.state ()
				  .account (key.pub)
				  .previous (open.hash ())
				  .balance (1)
				  .representative (key.pub)
				  .link (ledger.epoch_link (vban::epoch::epoch_1))
				  .sign (gen.prv, gen.pub)
				  .work (0)
				  .build (ec);
	ASSERT_FALSE (ec);

	process_block (send, {});
	process_block (receive, {});
	process_block (change, {});
	process_block (state, vban::block_details (vban::epoch::epoch_0, true, false, false));
	process_block (open, {});
	process_block (epoch, vban::block_details (vban::epoch::epoch_1, false, false, true));
}

TEST (ledger, dependents_confirmed)
{
	vban::block_builder builder;
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *genesis.open));
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	auto send1 = builder.state ()
				 .account (vban::genesis_account)
				 .previous (genesis.hash ())
				 .representative (vban::genesis_account)
				 .balance (vban::genesis_amount - 100)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*pool.generate (genesis.hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *send1).code);
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *send1));
	auto send2 = builder.state ()
				 .account (vban::genesis_account)
				 .previous (send1->hash ())
				 .representative (vban::genesis_account)
				 .balance (vban::genesis_amount - 200)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *send2).code);
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *send2));
	auto receive1 = builder.state ()
					.account (key1.pub)
					.previous (0)
					.representative (vban::genesis_account)
					.balance (100)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (key1.pub))
					.build_shared ();
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *receive1).code);
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *receive1));
	vban::confirmation_height_info height;
	ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, vban::genesis_account, height));
	height.height += 1;
	ledger.store.confirmation_height_put (transaction, vban::genesis_account, height);
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *receive1));
	auto receive2 = builder.state ()
					.account (key1.pub)
					.previous (receive1->hash ())
					.representative (vban::genesis_account)
					.balance (200)
					.link (send2->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (receive1->hash ()))
					.build_shared ();
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *receive2).code);
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *receive2));
	ASSERT_TRUE (ledger.store.confirmation_height_get (transaction, key1.pub, height));
	height.height += 1;
	ledger.store.confirmation_height_put (transaction, key1.pub, height);
	ASSERT_FALSE (ledger.dependents_confirmed (transaction, *receive2));
	ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, vban::genesis_account, height));
	height.height += 1;
	ledger.store.confirmation_height_put (transaction, vban::genesis_account, height);
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *receive2));
}

TEST (ledger, dependents_confirmed_pruning)
{
	vban::block_builder builder;
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	auto send1 = builder.state ()
				 .account (vban::genesis_account)
				 .previous (genesis.hash ())
				 .representative (vban::genesis_account)
				 .balance (vban::genesis_amount - 100)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*pool.generate (genesis.hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *send1).code);
	auto send2 = builder.state ()
				 .account (vban::genesis_account)
				 .previous (send1->hash ())
				 .representative (vban::genesis_account)
				 .balance (vban::genesis_amount - 200)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*pool.generate (send1->hash ()))
				 .build_shared ();
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *send2).code);
	vban::confirmation_height_info height;
	ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, vban::genesis_account, height));
	height.height = 3;
	ledger.store.confirmation_height_put (transaction, vban::genesis_account, height);
	ASSERT_TRUE (ledger.block_confirmed (transaction, send1->hash ()));
	ASSERT_EQ (2, ledger.pruning_action (transaction, send2->hash (), 1));
	auto receive1 = builder.state ()
					.account (key1.pub)
					.previous (0)
					.representative (vban::genesis_account)
					.balance (100)
					.link (send1->hash ())
					.sign (key1.prv, key1.pub)
					.work (*pool.generate (key1.pub))
					.build_shared ();
	ASSERT_TRUE (ledger.dependents_confirmed (transaction, *receive1));
}

TEST (ledger, block_confirmed)
{
	vban::block_builder builder;
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	store->initialize (transaction, genesis, ledger.cache);
	ASSERT_TRUE (ledger.block_confirmed (transaction, genesis.open->hash ()));
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::keypair key1;
	auto send1 = builder.state ()
				 .account (vban::genesis_account)
				 .previous (genesis.hash ())
				 .representative (vban::genesis_account)
				 .balance (vban::genesis_amount - 100)
				 .link (key1.pub)
				 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				 .work (*pool.generate (genesis.hash ()))
				 .build ();
	// Must be safe against non-existing blocks
	ASSERT_FALSE (ledger.block_confirmed (transaction, send1->hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *send1).code);
	ASSERT_FALSE (ledger.block_confirmed (transaction, send1->hash ()));
	vban::confirmation_height_info height;
	ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, vban::genesis_account, height));
	++height.height;
	ledger.store.confirmation_height_put (transaction, vban::genesis_account, height);
	ASSERT_TRUE (ledger.block_confirmed (transaction, send1->hash ()));
}

TEST (ledger, cache)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	store->initialize (store->tx_begin_write (), genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::block_builder builder;

	size_t const total = 100;

	// Check existing ledger (incremental cache update) and reload on a new ledger
	for (size_t i (0); i < total; ++i)
	{
		auto account_count = 1 + i;
		auto block_count = 1 + 2 * (i + 1) - 2;
		auto cemented_count = 1 + 2 * (i + 1) - 2;
		auto genesis_weight = vban::genesis_amount - i;
		auto pruned_count = i;

		auto cache_check = [&, i] (vban::ledger_cache const & cache_a) {
			ASSERT_EQ (account_count, cache_a.account_count);
			ASSERT_EQ (block_count, cache_a.block_count);
			ASSERT_EQ (cemented_count, cache_a.cemented_count);
			ASSERT_EQ (genesis_weight, cache_a.rep_weights.representation_get (vban::genesis_account));
			ASSERT_EQ (pruned_count, cache_a.pruned_count);
		};

		vban::keypair key;
		auto const latest = ledger.latest (store->tx_begin_read (), vban::genesis_account);
		auto send = builder.state ()
					.account (vban::genesis_account)
					.previous (latest)
					.representative (vban::genesis_account)
					.balance (vban::genesis_amount - (i + 1))
					.link (key.pub)
					.sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
					.work (*pool.generate (latest))
					.build ();
		auto open = builder.state ()
					.account (key.pub)
					.previous (0)
					.representative (key.pub)
					.balance (1)
					.link (send->hash ())
					.sign (key.prv, key.pub)
					.work (*pool.generate (key.pub))
					.build ();
		{
			auto transaction (store->tx_begin_write ());
			ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *send).code);
		}

		++block_count;
		--genesis_weight;
		cache_check (ledger.cache);
		cache_check (vban::ledger (*store, stats).cache);

		{
			auto transaction (store->tx_begin_write ());
			ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, *open).code);
		}

		++block_count;
		++account_count;
		cache_check (ledger.cache);
		cache_check (vban::ledger (*store, stats).cache);

		{
			auto transaction (store->tx_begin_write ());
			vban::confirmation_height_info height;
			ASSERT_FALSE (ledger.store.confirmation_height_get (transaction, vban::genesis_account, height));
			++height.height;
			height.frontier = send->hash ();
			ledger.store.confirmation_height_put (transaction, vban::genesis_account, height);
			ASSERT_TRUE (ledger.block_confirmed (transaction, send->hash ()));
			++ledger.cache.cemented_count;
		}

		++cemented_count;
		cache_check (ledger.cache);
		cache_check (vban::ledger (*store, stats).cache);

		{
			auto transaction (store->tx_begin_write ());
			vban::confirmation_height_info height;
			ledger.store.confirmation_height_get (transaction, key.pub, height);
			height.height += 1;
			height.frontier = open->hash ();
			ledger.store.confirmation_height_put (transaction, key.pub, height);
			ASSERT_TRUE (ledger.block_confirmed (transaction, open->hash ()));
			++ledger.cache.cemented_count;
		}

		++cemented_count;
		cache_check (ledger.cache);
		cache_check (vban::ledger (*store, stats).cache);

		{
			auto transaction (store->tx_begin_write ());
			ledger.store.pruned_put (transaction, open->hash ());
			++ledger.cache.pruned_count;
		}
		++pruned_count;
		cache_check (ledger.cache);
		cache_check (vban::ledger (*store, stats).cache);
	}
}

TEST (ledger, pruning_action)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	auto send1_stored (store->block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send1_stored);
	ASSERT_EQ (send1, *send1_stored);
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	vban::state_block send2 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 2, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	// Pruning action
	ASSERT_EQ (1, ledger.pruning_action (transaction, send1.hash (), 1));
	ASSERT_EQ (0, ledger.pruning_action (transaction, genesis.hash (), 1));
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_TRUE (ledger.block_or_pruned_exists (transaction, send1.hash ()));
	// Pruned ledger start without proper flags emulation
	ledger.pruning = false;
	ASSERT_TRUE (ledger.block_or_pruned_exists (transaction, send1.hash ()));
	ledger.pruning = true;
	ASSERT_TRUE (store->pruned_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, genesis.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	// Receiving pruned block
	vban::state_block receive1 (vban::genesis_account, send2.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store->block_exists (transaction, receive1.hash ()));
	auto receive1_stored (store->block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive1_stored);
	ASSERT_EQ (receive1, *receive1_stored);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_EQ (4, receive1_stored->sideband ().height);
	ASSERT_FALSE (receive1_stored->sideband ().details.is_send);
	ASSERT_TRUE (receive1_stored->sideband ().details.is_receive);
	ASSERT_FALSE (receive1_stored->sideband ().details.is_epoch);
	// Middle block pruning
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	ASSERT_EQ (1, ledger.pruning_action (transaction, send2.hash (), 1));
	ASSERT_TRUE (store->pruned_exists (transaction, send2.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send2.hash ()));
	ASSERT_EQ (store->account_count (transaction), ledger.cache.account_count);
	ASSERT_EQ (store->pruned_count (transaction), ledger.cache.pruned_count);
	ASSERT_EQ (store->block_count (transaction), ledger.cache.block_count - ledger.cache.pruned_count);
}

TEST (ledger, pruning_large_chain)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	size_t send_receive_pairs (20);
	auto last_hash (genesis.hash ());
	for (auto i (0); i < send_receive_pairs; i++)
	{
		vban::state_block send (vban::genesis_account, last_hash, vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (last_hash));
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
		ASSERT_TRUE (store->block_exists (transaction, send.hash ()));
		vban::state_block receive (vban::genesis_account, send.hash (), vban::genesis_account, vban::genesis_amount, send.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send.hash ()));
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive).code);
		ASSERT_TRUE (store->block_exists (transaction, receive.hash ()));
		last_hash = receive.hash ();
	}
	ASSERT_EQ (0, store->pruned_count (transaction));
	ASSERT_EQ (send_receive_pairs * 2 + 1, store->block_count (transaction));
	// Pruning action
	ASSERT_EQ (send_receive_pairs * 2, ledger.pruning_action (transaction, last_hash, 5));
	ASSERT_TRUE (store->pruned_exists (transaction, last_hash));
	ASSERT_TRUE (store->block_exists (transaction, genesis.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, last_hash));
	ASSERT_EQ (store->pruned_count (transaction), ledger.cache.pruned_count);
	ASSERT_EQ (store->block_count (transaction), ledger.cache.block_count - ledger.cache.pruned_count);
	ASSERT_EQ (send_receive_pairs * 2, store->pruned_count (transaction));
	ASSERT_EQ (1, store->block_count (transaction)); // Genesis
}

TEST (ledger, pruning_source_rollback)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block epoch1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch1).code);
	vban::state_block send1 (vban::genesis_account, epoch1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	vban::state_block send2 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 2, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	// Pruning action
	ASSERT_EQ (2, ledger.pruning_action (transaction, send1.hash (), 1));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, epoch1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, epoch1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, genesis.hash ()));
	vban::pending_info info;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (vban::genesis_account, send1.hash ()), info));
	ASSERT_EQ (vban::genesis_account, info.source);
	ASSERT_EQ (vban::Gxrb_ratio, info.amount.number ());
	ASSERT_EQ (vban::epoch::epoch_1, info.epoch);
	// Receiving pruned block
	vban::state_block receive1 (vban::genesis_account, send2.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_EQ (2, ledger.cache.pruned_count);
	ASSERT_EQ (5, ledger.cache.block_count);
	// Rollback receive block
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	vban::pending_info info2;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (vban::genesis_account, send1.hash ()), info2));
	ASSERT_NE (vban::genesis_account, info2.source); // Tradeoff to not store pruned blocks accounts
	ASSERT_EQ (vban::Gxrb_ratio, info2.amount.number ());
	ASSERT_EQ (vban::epoch::epoch_1, info2.epoch);
	// Process receive block again
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_EQ (2, ledger.cache.pruned_count);
	ASSERT_EQ (5, ledger.cache.block_count);
}

TEST (ledger, pruning_source_rollback_legacy)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send1 (genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	vban::keypair key1;
	vban::send_block send2 (send1.hash (), key1.pub, vban::genesis_amount - 2 * vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (key1.pub, send2.hash ())));
	vban::send_block send3 (send2.hash (), vban::genesis_account, vban::genesis_amount - 3 * vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send3).code);
	ASSERT_TRUE (store->block_exists (transaction, send3.hash ()));
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send3.hash ())));
	// Pruning action
	ASSERT_EQ (2, ledger.pruning_action (transaction, send2.hash (), 1));
	ASSERT_FALSE (store->block_exists (transaction, send2.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, send2.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, genesis.hash ()));
	vban::pending_info info1;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (vban::genesis_account, send1.hash ()), info1));
	ASSERT_EQ (vban::genesis_account, info1.source);
	ASSERT_EQ (vban::Gxrb_ratio, info1.amount.number ());
	ASSERT_EQ (vban::epoch::epoch_0, info1.epoch);
	vban::pending_info info2;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (key1.pub, send2.hash ()), info2));
	ASSERT_EQ (vban::genesis_account, info2.source);
	ASSERT_EQ (vban::Gxrb_ratio, info2.amount.number ());
	ASSERT_EQ (vban::epoch::epoch_0, info2.epoch);
	// Receiving pruned block
	vban::receive_block receive1 (send3.hash (), send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send3.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_EQ (2, ledger.cache.pruned_count);
	ASSERT_EQ (5, ledger.cache.block_count);
	// Rollback receive block
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	vban::pending_info info3;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (vban::genesis_account, send1.hash ()), info3));
	ASSERT_NE (vban::genesis_account, info3.source); // Tradeoff to not store pruned blocks accounts
	ASSERT_EQ (vban::Gxrb_ratio, info3.amount.number ());
	ASSERT_EQ (vban::epoch::epoch_0, info3.epoch);
	// Process receive block again
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	ASSERT_EQ (2, ledger.cache.pruned_count);
	ASSERT_EQ (5, ledger.cache.block_count);
	// Receiving pruned block (open)
	vban::open_block open1 (send2.hash (), vban::genesis_account, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (key1.pub, send2.hash ())));
	ASSERT_EQ (2, ledger.cache.pruned_count);
	ASSERT_EQ (6, ledger.cache.block_count);
	// Rollback open block
	ASSERT_FALSE (ledger.rollback (transaction, open1.hash ()));
	vban::pending_info info4;
	ASSERT_FALSE (store->pending_get (transaction, vban::pending_key (key1.pub, send2.hash ()), info4));
	ASSERT_NE (vban::genesis_account, info4.source); // Tradeoff to not store pruned blocks accounts
	ASSERT_EQ (vban::Gxrb_ratio, info4.amount.number ());
	ASSERT_EQ (vban::epoch::epoch_0, info4.epoch);
	// Process open block again
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store->pending_exists (transaction, vban::pending_key (key1.pub, send2.hash ())));
	ASSERT_EQ (2, ledger.cache.pruned_count);
	ASSERT_EQ (6, ledger.cache.block_count);
}

TEST (ledger, pruning_process_error)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_EQ (0, ledger.cache.pruned_count);
	ASSERT_EQ (2, ledger.cache.block_count);
	// Pruning action for latest block (not valid action)
	ASSERT_EQ (1, ledger.pruning_action (transaction, send1.hash (), 1));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, send1.hash ()));
	// Attempt to process pruned block again
	ASSERT_EQ (vban::process_result::old, ledger.process (transaction, send1).code);
	// Attept to process new block after pruned
	vban::state_block send2 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 2, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::gap_previous, ledger.process (transaction, send2).code);
	ASSERT_EQ (1, ledger.cache.pruned_count);
	ASSERT_EQ (2, ledger.cache.block_count);
}

TEST (ledger, pruning_legacy_blocks)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	vban::keypair key1;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send1 (genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->pending_exists (transaction, vban::pending_key (vban::genesis_account, send1.hash ())));
	vban::receive_block receive1 (send1.hash (), send1.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive1).code);
	vban::change_block change1 (receive1.hash (), key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (receive1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change1).code);
	vban::send_block send2 (change1.hash (), key1.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (change1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	vban::open_block open1 (send2.hash (), vban::genesis_account, key1.pub, key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open1).code);
	vban::send_block send3 (open1.hash (), vban::genesis_account, 0, key1.prv, key1.pub, *pool.generate (open1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send3).code);
	// Pruning action
	ASSERT_EQ (3, ledger.pruning_action (transaction, change1.hash (), 2));
	ASSERT_EQ (1, ledger.pruning_action (transaction, open1.hash (), 1));
	ASSERT_TRUE (store->block_exists (transaction, genesis.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, send1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, receive1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, receive1.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, change1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, change1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	ASSERT_FALSE (store->block_exists (transaction, open1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, open1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, send3.hash ()));
	ASSERT_EQ (4, ledger.cache.pruned_count);
	ASSERT_EQ (7, ledger.cache.block_count);
	ASSERT_EQ (store->pruned_count (transaction), ledger.cache.pruned_count);
	ASSERT_EQ (store->block_count (transaction), ledger.cache.block_count - ledger.cache.pruned_count);
}

TEST (ledger, pruning_safe_functions)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	vban::state_block send2 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 2, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	// Pruning action
	ASSERT_EQ (1, ledger.pruning_action (transaction, send1.hash (), 1));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_TRUE (ledger.block_or_pruned_exists (transaction, send1.hash ())); // true for pruned
	ASSERT_TRUE (store->pruned_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, genesis.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	// Safe ledger actions
	bool error (false);
	ASSERT_EQ (0, ledger.balance_safe (transaction, send1.hash (), error));
	ASSERT_TRUE (error);
	error = false;
	ASSERT_EQ (vban::genesis_amount - vban::Gxrb_ratio * 2, ledger.balance_safe (transaction, send2.hash (), error));
	ASSERT_FALSE (error);
	error = false;
	ASSERT_EQ (0, ledger.amount_safe (transaction, send2.hash (), error));
	ASSERT_TRUE (error);
	error = false;
	ASSERT_TRUE (ledger.account_safe (transaction, send1.hash (), error).is_zero ());
	ASSERT_TRUE (error);
	error = false;
	ASSERT_EQ (vban::genesis_account, ledger.account_safe (transaction, send2.hash (), error));
	ASSERT_FALSE (error);
}

TEST (ledger, hash_root_random)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	ledger.pruning = true;
	vban::genesis genesis;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block send1 (vban::genesis_account, genesis.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store->block_exists (transaction, send1.hash ()));
	vban::state_block send2 (vban::genesis_account, send1.hash (), vban::genesis_account, vban::genesis_amount - vban::Gxrb_ratio * 2, vban::genesis_account, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	// Pruning action
	ASSERT_EQ (1, ledger.pruning_action (transaction, send1.hash (), 1));
	ASSERT_FALSE (store->block_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->pruned_exists (transaction, send1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, genesis.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, send2.hash ()));
	// Test random block including pruned
	bool done (false);
	auto iteration (0);
	while (!done)
	{
		++iteration;
		auto root_hash (ledger.hash_root_random (transaction));
		done = (root_hash.first == send1.hash ()) && (root_hash.second.is_zero ());
		ASSERT_LE (iteration, 1000);
	}
	done = false;
	while (!done)
	{
		++iteration;
		auto root_hash (ledger.hash_root_random (transaction));
		done = (root_hash.first == send2.hash ()) && (root_hash.second == send2.root ().as_block_hash ());
		ASSERT_LE (iteration, 1000);
	}
}

TEST (ledger, migrate_lmdb_to_rocksdb)
{
	auto path (vban::unique_path ());
	vban::genesis genesis;
	vban::logger_mt logger;
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	vban::mdb_store store (logger, path / "data.ldb");
	vban::stat stats;
	vban::ledger ledger (store, stats);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());

	auto send = vban::state_block_builder ()
				.account (vban::dev_genesis_key.pub)
				.previous (vban::genesis_hash)
				.representative (0)
				.link (vban::account (10))
				.balance (vban::genesis_amount - 100)
				.sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				.work (*pool.generate (vban::genesis_hash))
				.build_shared ();

	vban::endpoint_key endpoint_key (address.to_bytes (), port);
	auto version = 99;

	{
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		ASSERT_FALSE (store.init_error ());

		// Lower the database to the max version unsupported for upgrades
		store.confirmation_height_put (transaction, vban::genesis_account, { 2, send->hash () });

		store.online_weight_put (transaction, 100, vban::amount (2));
		store.frontier_put (transaction, vban::block_hash (2), vban::account (5));
		store.peer_put (transaction, endpoint_key);

		store.pending_put (transaction, vban::pending_key (vban::genesis_account, send->hash ()), vban::pending_info (vban::genesis_account, 100, vban::epoch::epoch_0));
		store.pruned_put (transaction, send->hash ());
		store.unchecked_put (transaction, vban::genesis_hash, send);
		store.version_put (transaction, version);
		send->sideband_set ({});
		store.block_put (transaction, send->hash (), *send);
		store.final_vote_put (transaction, send->qualified_root (), vban::block_hash (2));
	}

	auto error = ledger.migrate_lmdb_to_rocksdb (path);
	ASSERT_FALSE (error);

	vban::rocksdb_store rocksdb_store (logger, path / "rocksdb");
	auto rocksdb_transaction (rocksdb_store.tx_begin_read ());

	vban::pending_info pending_info;
	ASSERT_FALSE (rocksdb_store.pending_get (rocksdb_transaction, vban::pending_key (vban::genesis_account, send->hash ()), pending_info));

	for (auto i = rocksdb_store.online_weight_begin (rocksdb_transaction); i != rocksdb_store.online_weight_end (); ++i)
	{
		ASSERT_EQ (i->first, 100);
		ASSERT_EQ (i->second, 2);
	}

	ASSERT_EQ (rocksdb_store.online_weight_count (rocksdb_transaction), 1);

	auto block1 = rocksdb_store.block_get (rocksdb_transaction, send->hash ());

	ASSERT_EQ (*send, *block1);
	ASSERT_TRUE (rocksdb_store.peer_exists (rocksdb_transaction, endpoint_key));
	ASSERT_EQ (rocksdb_store.version_get (rocksdb_transaction), version);
	ASSERT_EQ (rocksdb_store.frontier_get (rocksdb_transaction, 2), 5);
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (rocksdb_store.confirmation_height_get (rocksdb_transaction, vban::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 2);
	ASSERT_EQ (confirmation_height_info.frontier, send->hash ());
	ASSERT_TRUE (rocksdb_store.final_vote_get (rocksdb_transaction, vban::root (send->previous ())).size () == 1);
	ASSERT_EQ (rocksdb_store.final_vote_get (rocksdb_transaction, vban::root (send->previous ()))[0], vban::block_hash (2));

	auto unchecked_infos = rocksdb_store.unchecked_get (rocksdb_transaction, vban::genesis_hash);
	ASSERT_EQ (unchecked_infos.size (), 1);
	ASSERT_EQ (unchecked_infos.front ().account, vban::genesis_account);
	ASSERT_EQ (*unchecked_infos.front ().block, *send);
}

TEST (ledger, unconfirmed_frontiers)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	vban::ledger ledger (*store, stats);
	vban::genesis genesis;
	store->initialize (store->tx_begin_write (), genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());

	auto unconfirmed_frontiers = ledger.unconfirmed_frontiers ();
	ASSERT_TRUE (unconfirmed_frontiers.empty ());

	vban::state_block_builder builder;
	vban::keypair key;
	auto const latest = ledger.latest (store->tx_begin_read (), vban::genesis_account);
	auto send = builder.make_block ()
				.account (vban::genesis_account)
				.previous (latest)
				.representative (vban::genesis_account)
				.balance (vban::genesis_amount - 100)
				.link (key.pub)
				.sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
				.work (*pool.generate (latest))
				.build ();

	ASSERT_EQ (vban::process_result::progress, ledger.process (store->tx_begin_write (), *send).code);

	unconfirmed_frontiers = ledger.unconfirmed_frontiers ();
	ASSERT_EQ (unconfirmed_frontiers.size (), 1);
	ASSERT_EQ (unconfirmed_frontiers.begin ()->first, 1);
	vban::uncemented_info uncemented_info1{ latest, send->hash (), vban::genesis_account };
	auto uncemented_info2 = unconfirmed_frontiers.begin ()->second;
	ASSERT_EQ (uncemented_info1.account, uncemented_info2.account);
	ASSERT_EQ (uncemented_info1.cemented_frontier, uncemented_info2.cemented_frontier);
	ASSERT_EQ (uncemented_info1.frontier, uncemented_info2.frontier);
}
