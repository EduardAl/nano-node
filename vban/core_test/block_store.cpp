#include <vban/crypto_lib/random_pool.hpp>
#include <vban/lib/lmdbconfig.hpp>
#include <vban/lib/logger_mt.hpp>
#include <vban/lib/stats.hpp>
#include <vban/lib/utility.hpp>
#include <vban/lib/work.hpp>
#include <vban/node/common.hpp>
#include <vban/node/lmdb/lmdb.hpp>
#include <vban/node/rocksdb/rocksdb.hpp>
#include <vban/node/testing.hpp>
#include <vban/secure/ledger.hpp>
#include <vban/secure/utility.hpp>
#include <vban/secure/versioning.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <boost/filesystem.hpp>

#include <fstream>
#include <unordered_set>

#include <stdlib.h>

namespace
{
void modify_account_info_to_v14 (vban::mdb_store & store, vban::transaction const & transaction_a, vban::account const & account_a, uint64_t confirmation_height, vban::block_hash const & rep_block);
void modify_confirmation_height_to_v15 (vban::mdb_store & store, vban::transaction const & transaction, vban::account const & account, uint64_t confirmation_height);
void write_sideband_v14 (vban::mdb_store & store_a, vban::transaction & transaction_a, vban::block const & block_a, MDB_dbi db_a);
void write_sideband_v15 (vban::mdb_store & store_a, vban::transaction & transaction_a, vban::block const & block_a);
void write_block_w_sideband_v18 (vban::mdb_store & store_a, MDB_dbi database, vban::write_transaction & transaction_a, vban::block const & block_a);
}

TEST (block_store, construction)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
}

TEST (block_store, block_details)
{
	vban::block_details details_send (vban::epoch::epoch_0, true, false, false);
	ASSERT_TRUE (details_send.is_send);
	ASSERT_FALSE (details_send.is_receive);
	ASSERT_FALSE (details_send.is_epoch);
	ASSERT_EQ (vban::epoch::epoch_0, details_send.epoch);

	vban::block_details details_receive (vban::epoch::epoch_1, false, true, false);
	ASSERT_FALSE (details_receive.is_send);
	ASSERT_TRUE (details_receive.is_receive);
	ASSERT_FALSE (details_receive.is_epoch);
	ASSERT_EQ (vban::epoch::epoch_1, details_receive.epoch);

	vban::block_details details_epoch (vban::epoch::epoch_2, false, false, true);
	ASSERT_FALSE (details_epoch.is_send);
	ASSERT_FALSE (details_epoch.is_receive);
	ASSERT_TRUE (details_epoch.is_epoch);
	ASSERT_EQ (vban::epoch::epoch_2, details_epoch.epoch);

	vban::block_details details_none (vban::epoch::unspecified, false, false, false);
	ASSERT_FALSE (details_none.is_send);
	ASSERT_FALSE (details_none.is_receive);
	ASSERT_FALSE (details_none.is_epoch);
	ASSERT_EQ (vban::epoch::unspecified, details_none.epoch);
}

TEST (block_store, block_details_serialization)
{
	vban::block_details details1;
	details1.epoch = vban::epoch::epoch_2;
	details1.is_epoch = false;
	details1.is_receive = true;
	details1.is_send = false;
	std::vector<uint8_t> vector;
	{
		vban::vectorstream stream1 (vector);
		details1.serialize (stream1);
	}
	vban::bufferstream stream2 (vector.data (), vector.size ());
	vban::block_details details2;
	ASSERT_FALSE (details2.deserialize (stream2));
	ASSERT_EQ (details1, details2);
}

TEST (block_store, sideband_serialization)
{
	vban::block_sideband sideband1;
	sideband1.account = 1;
	sideband1.balance = 2;
	sideband1.height = 3;
	sideband1.successor = 4;
	sideband1.timestamp = 5;
	std::vector<uint8_t> vector;
	{
		vban::vectorstream stream1 (vector);
		sideband1.serialize (stream1, vban::block_type::receive);
	}
	vban::bufferstream stream2 (vector.data (), vector.size ());
	vban::block_sideband sideband2;
	ASSERT_FALSE (sideband2.deserialize (stream2, vban::block_type::receive));
	ASSERT_EQ (sideband1.account, sideband2.account);
	ASSERT_EQ (sideband1.balance, sideband2.balance);
	ASSERT_EQ (sideband1.height, sideband2.height);
	ASSERT_EQ (sideband1.successor, sideband2.successor);
	ASSERT_EQ (sideband1.timestamp, sideband2.timestamp);
}

TEST (block_store, add_item)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::open_block block (0, 1, 0, vban::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store->block_exists (transaction, hash1));
	store->block_put (transaction, hash1, block);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
	ASSERT_TRUE (store->block_exists (transaction, hash1));
	ASSERT_FALSE (store->block_exists (transaction, hash1.number () - 1));
	store->block_del (transaction, hash1);
	auto latest3 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, clear_successor)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::open_block block1 (0, 1, 0, vban::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, block1.hash (), block1);
	vban::open_block block2 (0, 2, 0, vban::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	store->block_put (transaction, block2.hash (), block2);
	auto block2_store (store->block_get (transaction, block1.hash ()));
	ASSERT_NE (nullptr, block2_store);
	ASSERT_EQ (0, block2_store->sideband ().successor.number ());
	auto modified_sideband = block2_store->sideband ();
	modified_sideband.successor = block2.hash ();
	block1.sideband_set (modified_sideband);
	store->block_put (transaction, block1.hash (), block1);
	{
		auto block1_store (store->block_get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (block2.hash (), block1_store->sideband ().successor);
	}
	store->block_successor_clear (transaction, block1.hash ());
	{
		auto block1_store (store->block_get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block1_store);
		ASSERT_EQ (0, block1_store->sideband ().successor.number ());
	}
}

TEST (block_store, add_nonempty_block)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::keypair key1;
	vban::open_block block (0, 1, 0, vban::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	block.signature = vban::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block_put (transaction, hash1, block);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::keypair key1;
	vban::open_block block (0, 1, 1, vban::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	block.signature = vban::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store->tx_begin_write ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	vban::open_block block2 (0, 1, 3, vban::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	block2.hashables.account = 3;
	auto hash2 (block2.hash ());
	block2.signature = vban::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store->block_get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	store->block_put (transaction, hash1, block);
	store->block_put (transaction, hash2, block2);
	auto latest3 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (block, *latest3);
	auto latest4 (store->block_get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::keypair key1;
	vban::keypair key2;
	vban::open_block block1 (0, 1, 0, vban::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, block1.hash (), block1);
	vban::receive_block block (block1.hash (), 1, vban::keypair ().prv, 2, 3);
	block.sideband_set ({});
	vban::block_hash hash1 (block.hash ());
	auto latest1 (store->block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	store->block_put (transaction, hash1, block);
	auto latest2 (store->block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::keypair key1;
	vban::pending_key key2 (0, 0);
	vban::pending_info pending1;
	auto transaction (store->tx_begin_write ());
	ASSERT_TRUE (store->pending_get (transaction, key2, pending1));
	store->pending_put (transaction, key2, pending1);
	vban::pending_info pending2;
	ASSERT_FALSE (store->pending_get (transaction, key2, pending2));
	ASSERT_EQ (pending1, pending2);
	store->pending_del (transaction, key2);
	ASSERT_TRUE (store->pending_get (transaction, key2, pending2));
}

TEST (block_store, pending_iterator)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	ASSERT_EQ (store->pending_end (), store->pending_begin (transaction));
	store->pending_put (transaction, vban::pending_key (1, 2), { 2, 3, vban::epoch::epoch_1 });
	auto current (store->pending_begin (transaction));
	ASSERT_NE (store->pending_end (), current);
	vban::pending_key key1 (current->first);
	ASSERT_EQ (vban::account (1), key1.account);
	ASSERT_EQ (vban::block_hash (2), key1.hash);
	vban::pending_info pending (current->second);
	ASSERT_EQ (vban::account (2), pending.source);
	ASSERT_EQ (vban::amount (3), pending.amount);
	ASSERT_EQ (vban::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::stat stats;
	auto transaction (store->tx_begin_write ());
	// Populate pending
	store->pending_put (transaction, vban::pending_key (vban::account (3), vban::block_hash (1)), vban::pending_info (vban::account (10), vban::amount (1), vban::epoch::epoch_0));
	store->pending_put (transaction, vban::pending_key (vban::account (3), vban::block_hash (4)), vban::pending_info (vban::account (10), vban::amount (0), vban::epoch::epoch_0));
	// Populate pending_v1
	store->pending_put (transaction, vban::pending_key (vban::account (2), vban::block_hash (2)), vban::pending_info (vban::account (10), vban::amount (2), vban::epoch::epoch_1));
	store->pending_put (transaction, vban::pending_key (vban::account (2), vban::block_hash (3)), vban::pending_info (vban::account (10), vban::amount (3), vban::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		vban::account begin (3);
		vban::account end (begin.number () + 1);
		for (auto i (store->pending_begin (transaction, vban::pending_key (begin, 0))), n (store->pending_begin (transaction, vban::pending_key (end, 0))); i != n; ++i, ++count)
		{
			vban::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		vban::account begin (2);
		vban::account end (begin.number () + 1);
		for (auto i (store->pending_begin (transaction, vban::pending_key (begin, 0))), n (store->pending_begin (transaction, vban::pending_key (end, 0))); i != n; ++i, ++count)
		{
			vban::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::genesis genesis;
	auto hash (genesis.hash ());
	vban::ledger_cache ledger_cache;
	auto transaction (store->tx_begin_write ());
	store->initialize (transaction, genesis, ledger_cache);
	vban::account_info info;
	ASSERT_FALSE (store->account_get (transaction, vban::genesis_account, info));
	ASSERT_EQ (hash, info.head);
	auto block1 (store->block_get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<vban::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, vban::seconds_since_epoch ());
	ASSERT_EQ (info.block_count, 1);
	// Genesis block should be confirmed by default
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, vban::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, hash);
	auto dev_pub_text (vban::dev_genesis_key.pub.to_string ());
	auto dev_pub_account (vban::dev_genesis_key.pub.to_account ());
	auto dev_prv_text (vban::dev_genesis_key.prv.to_string ());
	ASSERT_EQ (vban::genesis_account, vban::dev_genesis_key.pub);
}

TEST (bootstrap, simple)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<vban::send_block> (0, 1, 2, vban::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	auto block2 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store->unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	ASSERT_EQ (*block1, *(block3[0].block));
	store->unchecked_del (transaction, vban::unchecked_key (block1->previous (), block1->hash ()));
	auto block4 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block4.empty ());
}

TEST (unchecked, multiple)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<vban::send_block> (4, 1, 2, vban::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	auto block2 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store->unchecked_put (transaction, block1->previous (), block1);
	store->unchecked_put (transaction, block1->source (), block1);
	auto block3 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	auto block4 (store->unchecked_get (transaction, block1->source ()));
	ASSERT_FALSE (block4.empty ());
}

TEST (unchecked, double_put)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<vban::send_block> (4, 1, 2, vban::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	auto block2 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store->unchecked_put (transaction, block1->previous (), block1);
	store->unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (block3.size (), 1);
}

TEST (unchecked, multiple_get)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<vban::send_block> (4, 1, 2, vban::keypair ().prv, 4, 5));
	auto block2 (std::make_shared<vban::send_block> (3, 1, 2, vban::keypair ().prv, 4, 5));
	auto block3 (std::make_shared<vban::send_block> (5, 1, 2, vban::keypair ().prv, 4, 5));
	{
		auto transaction (store->tx_begin_write ());
		store->unchecked_put (transaction, block1->previous (), block1); // unchecked1
		store->unchecked_put (transaction, block1->hash (), block1); // unchecked2
		store->unchecked_put (transaction, block2->previous (), block2); // unchecked3
		store->unchecked_put (transaction, block1->previous (), block2); // unchecked1
		store->unchecked_put (transaction, block1->hash (), block2); // unchecked2
		store->unchecked_put (transaction, block3->previous (), block3);
		store->unchecked_put (transaction, block3->hash (), block3); // unchecked4
		store->unchecked_put (transaction, block1->previous (), block3); // unchecked1
	}
	auto transaction (store->tx_begin_read ());
	auto unchecked_count (store->unchecked_count (transaction));
	ASSERT_EQ (unchecked_count, 8);
	std::vector<vban::block_hash> unchecked1;
	auto unchecked1_blocks (store->unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (unchecked1_blocks.size (), 3);
	for (auto & i : unchecked1_blocks)
	{
		unchecked1.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block1->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block2->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block3->hash ()) != unchecked1.end ());
	std::vector<vban::block_hash> unchecked2;
	auto unchecked2_blocks (store->unchecked_get (transaction, block1->hash ()));
	ASSERT_EQ (unchecked2_blocks.size (), 2);
	for (auto & i : unchecked2_blocks)
	{
		unchecked2.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block1->hash ()) != unchecked2.end ());
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block2->hash ()) != unchecked2.end ());
	auto unchecked3 (store->unchecked_get (transaction, block2->previous ()));
	ASSERT_EQ (unchecked3.size (), 1);
	ASSERT_EQ (unchecked3[0].block->hash (), block2->hash ());
	auto unchecked4 (store->unchecked_get (transaction, block3->hash ()));
	ASSERT_EQ (unchecked4.size (), 1);
	ASSERT_EQ (unchecked4[0].block->hash (), block3->hash ());
	auto unchecked5 (store->unchecked_get (transaction, block2->hash ()));
	ASSERT_EQ (unchecked5.size (), 0);
}

TEST (block_store, empty_accounts)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->accounts_begin (transaction));
	auto end (store->accounts_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::open_block block1 (0, 1, 0, vban::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, block1.hash (), block1);
	ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
}

TEST (block_store, empty_bootstrap)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_read ());
	auto begin (store->unchecked_begin (transaction));
	auto end (store->unchecked_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_bootstrap)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto block1 (std::make_shared<vban::send_block> (0, 1, 2, vban::keypair ().prv, 4, 5));
	auto transaction (store->tx_begin_write ());
	store->unchecked_put (transaction, block1->hash (), block1);
	auto begin (store->unchecked_begin (transaction));
	auto end (store->unchecked_end ());
	ASSERT_NE (end, begin);
	auto hash1 (begin->first.key ());
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks (store->unchecked_get (transaction, hash1));
	ASSERT_EQ (1, blocks.size ());
	auto block2 (blocks[0].block);
	ASSERT_EQ (*block1, *block2);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, unchecked_begin_search)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::keypair key0;
	vban::send_block block1 (0, 1, 2, key0.prv, key0.pub, 3);
	vban::send_block block2 (5, 6, 7, key0.prv, key0.pub, 8);
}

TEST (block_store, frontier_retrieval)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::account account1 (0);
	vban::account_info info1 (0, 0, 0, 0, 0, 0, vban::epoch::epoch_0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, { 0, vban::block_hash (0) });
	store->account_put (transaction, account1, info1);
	vban::account_info info2;
	store->account_get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::account account (0);
	vban::block_hash hash (0);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account, { 20, vban::block_hash (15) });
	store->account_put (transaction, account, { hash, account, hash, 42, 100, 200, vban::epoch::epoch_0 });
	auto begin (store->accounts_begin (transaction));
	auto end (store->accounts_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, vban::account (begin->first));
	vban::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, account, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (vban::block_hash (15), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::open_block block1 (0, 1, 1, vban::keypair ().prv, 0, 0);
	block1.sideband_set ({});
	block1.hashables.account = 1;
	std::vector<vban::block_hash> hashes;
	std::vector<vban::open_block> blocks;
	hashes.push_back (block1.hash ());
	blocks.push_back (block1);
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, hashes[0], block1);
	vban::open_block block2 (0, 1, 2, vban::keypair ().prv, 0, 0);
	block2.sideband_set ({});
	hashes.push_back (block2.hash ());
	blocks.push_back (block2);
	store->block_put (transaction, hashes[1], block2);
	ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
	ASSERT_TRUE (store->block_exists (transaction, block2.hash ()));
}

TEST (block_store, two_account)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::account account1 (1);
	vban::block_hash hash1 (2);
	vban::account account2 (3);
	vban::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, { 20, vban::block_hash (10) });
	store->account_put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, vban::epoch::epoch_0 });
	store->confirmation_height_put (transaction, account2, { 30, vban::block_hash (20) });
	store->account_put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, vban::epoch::epoch_0 });
	auto begin (store->accounts_begin (transaction));
	auto end (store->accounts_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, vban::account (begin->first));
	vban::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store->confirmation_height_get (transaction, account1, confirmation_height_info));
	ASSERT_EQ (20, confirmation_height_info.height);
	ASSERT_EQ (vban::block_hash (10), confirmation_height_info.frontier);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, vban::account (begin->first));
	vban::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	ASSERT_FALSE (store->confirmation_height_get (transaction, account2, confirmation_height_info));
	ASSERT_EQ (30, confirmation_height_info.height);
	ASSERT_EQ (vban::block_hash (20), confirmation_height_info.frontier);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::account account1 (1);
	vban::block_hash hash1 (2);
	vban::account account2 (3);
	vban::block_hash hash2 (4);
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, account1, { 0, vban::block_hash (0) });
	store->account_put (transaction, account1, { hash1, account1, hash1, 100, 0, 300, vban::epoch::epoch_0 });
	store->confirmation_height_put (transaction, account2, { 0, vban::block_hash (0) });
	store->account_put (transaction, account2, { hash2, account2, hash2, 200, 0, 400, vban::epoch::epoch_0 });
	auto first (store->accounts_begin (transaction));
	auto second (store->accounts_begin (transaction));
	++second;
	auto find1 (store->accounts_begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store->accounts_begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store->accounts_begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

TEST (mdb_block_store, supported_version_upgrades)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	// Check that upgrading from an unsupported version is not supported
	auto path (vban::unique_path ());
	vban::genesis genesis;
	vban::logger_mt logger;
	{
		vban::mdb_store store (logger, path);
		vban::stat stats;
		vban::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		// Lower the database to the max version unsupported for upgrades
		store.version_put (transaction, store.minimum_version - 1);
	}

	// Upgrade should fail
	{
		vban::mdb_store store (logger, path);
		ASSERT_TRUE (store.init_error ());
	}

	auto path1 (vban::unique_path ());
	// Now try with the minimum version
	{
		vban::mdb_store store (logger, path1);
		vban::stat stats;
		vban::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		// Lower the database version to the minimum version supported for upgrade.
		store.version_put (transaction, store.minimum_version);
		store.confirmation_height_del (transaction, vban::genesis_account);
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks));
		modify_account_info_to_v14 (store, transaction, vban::genesis_account, 1, vban::genesis_hash);
		write_block_w_sideband_v18 (store, store.open_blocks, transaction, *vban::genesis ().open);
	}

	// Upgrade should work
	{
		vban::mdb_store store (logger, path1);
		ASSERT_FALSE (store.init_error ());
	}
}

TEST (mdb_block_store, bad_path)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	vban::logger_mt logger;
	vban::mdb_store store (logger, boost::filesystem::path ("///"));
	ASSERT_TRUE (store.init_error ());
}

TEST (block_store, DISABLED_already_open) // File can be shared
{
	auto path (vban::unique_path ());
	boost::filesystem::create_directories (path.parent_path ());
	vban::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	vban::logger_mt logger;
	auto store = vban::make_store (logger, path);
	ASSERT_TRUE (store->init_error ());
}

TEST (block_store, roots)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::send_block send_block (0, 1, 2, vban::keypair ().prv, 4, 5);
	ASSERT_EQ (send_block.hashables.previous, send_block.root ());
	vban::change_block change_block (0, 1, vban::keypair ().prv, 3, 4);
	ASSERT_EQ (change_block.hashables.previous, change_block.root ());
	vban::receive_block receive_block (0, 1, vban::keypair ().prv, 3, 4);
	ASSERT_EQ (receive_block.hashables.previous, receive_block.root ());
	vban::open_block open_block (0, 1, 2, vban::keypair ().prv, 4, 5);
	ASSERT_EQ (open_block.hashables.account, open_block.root ());
}

TEST (block_store, pending_exists)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::pending_key two (2, 0);
	vban::pending_info pending;
	auto transaction (store->tx_begin_write ());
	store->pending_put (transaction, two, pending);
	vban::pending_key one (1, 0);
	ASSERT_FALSE (store->pending_exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::account two (2);
	vban::account_info info;
	auto transaction (store->tx_begin_write ());
	store->confirmation_height_put (transaction, two, { 0, vban::block_hash (0) });
	store->account_put (transaction, two, info);
	vban::account one (1);
	ASSERT_FALSE (store->account_exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	std::unordered_set<vban::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store->tx_begin_write ());
		vban::account account;
		vban::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store->confirmation_height_put (transaction, account, { 0, vban::block_hash (0) });
		store->account_put (transaction, account, vban::account_info ());
	}
	std::unordered_set<vban::account> accounts2;
	vban::account previous (0);
	auto transaction (store->tx_begin_read ());
	for (auto i (store->accounts_begin (transaction, 0)), n (store->accounts_end ()); i != n; ++i)
	{
		vban::account current (i->first);
		ASSERT_GT (current.number (), previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
	// Reverse iteration
	std::unordered_set<vban::account> accounts3;
	previous = std::numeric_limits<vban::uint256_t>::max ();
	for (auto i (store->accounts_rbegin (transaction)), n (store->accounts_end ()); i != n; --i)
	{
		vban::account current (i->first);
		ASSERT_LT (current.number (), previous.number ());
		accounts3.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts3);
}

TEST (block_store, frontier)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	vban::block_hash hash (100);
	vban::account account (200);
	ASSERT_TRUE (store->frontier_get (transaction, hash).is_zero ());
	store->frontier_put (transaction, hash, account);
	ASSERT_EQ (account, store->frontier_get (transaction, hash));
	store->frontier_del (transaction, hash);
	ASSERT_TRUE (store->frontier_get (transaction, hash).is_zero ());
}

TEST (block_store, block_replace)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::send_block send1 (0, 0, 0, vban::keypair ().prv, 0, 1);
	send1.sideband_set ({});
	vban::send_block send2 (0, 0, 0, vban::keypair ().prv, 0, 2);
	send2.sideband_set ({});
	auto transaction (store->tx_begin_write ());
	store->block_put (transaction, 0, send1);
	store->block_put (transaction, 0, send2);
	auto block3 (store->block_get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->block_count (transaction));
		vban::open_block block (0, 1, 0, vban::keypair ().prv, 0, 0);
		block.sideband_set ({});
		auto hash1 (block.hash ());
		store->block_put (transaction, hash1, block);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->block_count (transaction));
}

TEST (block_store, account_count)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->account_count (transaction));
		vban::account account (200);
		store->confirmation_height_put (transaction, account, { 0, vban::block_hash (0) });
		store->account_put (transaction, account, vban::account_info ());
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (1, store->account_count (transaction));
}

TEST (block_store, cemented_count_cache)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	auto transaction (store->tx_begin_write ());
	vban::genesis genesis;
	vban::ledger_cache ledger_cache;
	store->initialize (transaction, genesis, ledger_cache);
	ASSERT_EQ (1, ledger_cache.cemented_count);
}

TEST (block_store, block_random)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::genesis genesis;
	{
		vban::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger_cache);
	}
	auto transaction (store->tx_begin_read ());
	auto block (store->block_random (transaction));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (*block, *genesis.open);
}

TEST (block_store, pruned_random)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());
	vban::genesis genesis;
	vban::open_block block (0, 1, 0, vban::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	{
		vban::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger_cache);
		store->pruned_put (transaction, hash1);
	}
	auto transaction (store->tx_begin_read ());
	auto random_hash (store->pruned_random (transaction));
	ASSERT_EQ (hash1, random_hash);
}

// Databases need to be dropped in order to convert to dupsort compatible
TEST (block_store, DISABLED_change_dupsort) // Unchecked is no longer dupsort table
{
	auto path (vban::unique_path ());
	vban::logger_mt logger;
	vban::mdb_store store (logger, path);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
	auto send1 (std::make_shared<vban::send_block> (0, 0, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	auto send2 (std::make_shared<vban::send_block> (1, 0, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, 0));
	ASSERT_NE (send1->hash (), send2->hash ());
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 0));
	mdb_dbi_close (store.env, store.unchecked);
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (block_store, state_block)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	vban::genesis genesis;
	vban::keypair key1;
	vban::state_block block1 (1, genesis.hash (), 3, 4, 6, key1.prv, key1.pub, 7);
	block1.sideband_set ({});
	{
		vban::ledger_cache ledger_cache;
		auto transaction (store->tx_begin_write ());
		store->initialize (transaction, genesis, ledger_cache);
		ASSERT_EQ (vban::block_type::state, block1.type ());
		store->block_put (transaction, block1.hash (), block1);
		ASSERT_TRUE (store->block_exists (transaction, block1.hash ()));
		auto block2 (store->block_get (transaction, block1.hash ()));
		ASSERT_NE (nullptr, block2);
		ASSERT_EQ (block1, *block2);
	}
	{
		auto transaction (store->tx_begin_write ());
		auto count (store->block_count (transaction));
		ASSERT_EQ (2, count);
		store->block_del (transaction, block1.hash ());
		ASSERT_FALSE (store->block_exists (transaction, block1.hash ()));
	}
	auto transaction (store->tx_begin_read ());
	auto count2 (store->block_count (transaction));
	ASSERT_EQ (1, count2);
}

TEST (mdb_block_store, sideband_height)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	vban::logger_mt logger;
	vban::genesis genesis;
	vban::keypair key1;
	vban::keypair key2;
	vban::keypair key3;
	vban::mdb_store store (logger, vban::unique_path ());
	ASSERT_FALSE (store.init_error ());
	vban::stat stat;
	vban::ledger ledger (store, stat);
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis, ledger.cache);
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send (genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	vban::receive_block receive (send.hash (), send.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive).code);
	vban::change_block change (receive.hash (), 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (receive.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change).code);
	vban::state_block state_send1 (vban::dev_genesis_key.pub, change.hash (), 0, vban::genesis_amount - vban::Gxrb_ratio, key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (change.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send1).code);
	vban::state_block state_send2 (vban::dev_genesis_key.pub, state_send1.hash (), 0, vban::genesis_amount - 2 * vban::Gxrb_ratio, key2.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_send1.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send2).code);
	vban::state_block state_send3 (vban::dev_genesis_key.pub, state_send2.hash (), 0, vban::genesis_amount - 3 * vban::Gxrb_ratio, key3.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_send2.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send3).code);
	vban::state_block state_open (key1.pub, 0, 0, vban::Gxrb_ratio, state_send1.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_open).code);
	vban::state_block epoch (key1.pub, state_open.hash (), 0, vban::Gxrb_ratio, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_open.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch).code);
	ASSERT_EQ (vban::epoch::epoch_1, store.block_version (transaction, epoch.hash ()));
	vban::state_block epoch_open (key2.pub, 0, 0, 0, ledger.epoch_link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (key2.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (vban::epoch::epoch_1, store.block_version (transaction, epoch_open.hash ()));
	vban::state_block state_receive (key2.pub, epoch_open.hash (), 0, vban::Gxrb_ratio, state_send2.hash (), key2.prv, key2.pub, *pool.generate (epoch_open.hash ()));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_receive).code);
	vban::open_block open (state_send3.hash (), vban::dev_genesis_key.pub, key3.pub, key3.prv, key3.pub, *pool.generate (key3.pub));
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, open).code);
	auto block1 (store.block_get (transaction, genesis.hash ()));
	ASSERT_EQ (block1->sideband ().height, 1);
	auto block2 (store.block_get (transaction, send.hash ()));
	ASSERT_EQ (block2->sideband ().height, 2);
	auto block3 (store.block_get (transaction, receive.hash ()));
	ASSERT_EQ (block3->sideband ().height, 3);
	auto block4 (store.block_get (transaction, change.hash ()));
	ASSERT_EQ (block4->sideband ().height, 4);
	auto block5 (store.block_get (transaction, state_send1.hash ()));
	ASSERT_EQ (block5->sideband ().height, 5);
	auto block6 (store.block_get (transaction, state_send2.hash ()));
	ASSERT_EQ (block6->sideband ().height, 6);
	auto block7 (store.block_get (transaction, state_send3.hash ()));
	ASSERT_EQ (block7->sideband ().height, 7);
	auto block8 (store.block_get (transaction, state_open.hash ()));
	ASSERT_EQ (block8->sideband ().height, 1);
	auto block9 (store.block_get (transaction, epoch.hash ()));
	ASSERT_EQ (block9->sideband ().height, 2);
	auto block10 (store.block_get (transaction, epoch_open.hash ()));
	ASSERT_EQ (block10->sideband ().height, 1);
	auto block11 (store.block_get (transaction, state_receive.hash ()));
	ASSERT_EQ (block11->sideband ().height, 2);
	auto block12 (store.block_get (transaction, open.hash ()));
	ASSERT_EQ (block12->sideband ().height, 1);
}

TEST (block_store, peers)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());

	vban::endpoint_key endpoint (boost::asio::ip::address_v6::any ().to_bytes (), 100);
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->peer_exists (transaction, endpoint));
		ASSERT_EQ (store->peer_count (transaction), 0);

		// Add one
		store->peer_put (transaction, endpoint);
		ASSERT_TRUE (store->peer_exists (transaction, endpoint));
	}

	// Confirm that it can be found
	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 1);
	}

	// Add another one and check that it (and the existing one) can be found
	vban::endpoint_key endpoint1 (boost::asio::ip::address_v6::any ().to_bytes (), 101);
	{
		auto transaction (store->tx_begin_write ());
		store->peer_put (transaction, endpoint1);
		ASSERT_TRUE (store->peer_exists (transaction, endpoint1)); // Check new peer is here
		ASSERT_TRUE (store->peer_exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 2);
	}

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->peer_del (transaction, endpoint1);
		ASSERT_FALSE (store->peer_exists (transaction, endpoint1)); // Confirm it no longer exists
		ASSERT_TRUE (store->peer_exists (transaction, endpoint)); // Check first peer is still here
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 1);
	}

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->peer_del (transaction, endpoint);
		ASSERT_FALSE (store->peer_exists (transaction, endpoint));
	}

	{
		auto transaction (store->tx_begin_read ());
		ASSERT_EQ (store->peer_count (transaction), 0);
	}
}

TEST (block_store, endpoint_key_byte_order)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::make_address_v6 ("::ffff:127.0.0.1"));
	uint16_t port = 100;
	vban::endpoint_key endpoint_key (address.to_bytes (), port);

	std::vector<uint8_t> bytes;
	{
		vban::vectorstream stream (bytes);
		vban::write (stream, endpoint_key);
	}

	// This checks that the endpoint is serialized as expected, with a size
	// of 18 bytes (16 for ipv6 address and 2 for port), both in network byte order.
	ASSERT_EQ (bytes.size (), 18);
	ASSERT_EQ (bytes[10], 0xff);
	ASSERT_EQ (bytes[11], 0xff);
	ASSERT_EQ (bytes[12], 127);
	ASSERT_EQ (bytes[bytes.size () - 2], 0);
	ASSERT_EQ (bytes.back (), 100);

	// Deserialize the same stream bytes
	vban::bufferstream stream1 (bytes.data (), bytes.size ());
	vban::endpoint_key endpoint_key1;
	vban::read (stream1, endpoint_key1);

	// This should be in network bytes order
	ASSERT_EQ (address.to_bytes (), endpoint_key1.address_bytes ());

	// This should be in host byte order
	ASSERT_EQ (port, endpoint_key1.port ());
}

TEST (block_store, online_weight)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_FALSE (store->init_error ());
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (0, store->online_weight_count (transaction));
		ASSERT_EQ (store->online_weight_end (), store->online_weight_begin (transaction));
		ASSERT_EQ (store->online_weight_end (), store->online_weight_rbegin (transaction));
		store->online_weight_put (transaction, 1, 2);
		store->online_weight_put (transaction, 3, 4);
	}
	{
		auto transaction (store->tx_begin_write ());
		ASSERT_EQ (2, store->online_weight_count (transaction));
		auto item (store->online_weight_begin (transaction));
		ASSERT_NE (store->online_weight_end (), item);
		ASSERT_EQ (1, item->first);
		ASSERT_EQ (2, item->second.number ());
		auto item_last (store->online_weight_rbegin (transaction));
		ASSERT_NE (store->online_weight_end (), item_last);
		ASSERT_EQ (3, item_last->first);
		ASSERT_EQ (4, item_last->second.number ());
		store->online_weight_del (transaction, 1);
		ASSERT_EQ (1, store->online_weight_count (transaction));
		ASSERT_EQ (store->online_weight_begin (transaction), store->online_weight_rbegin (transaction));
		store->online_weight_del (transaction, 3);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (0, store->online_weight_count (transaction));
	ASSERT_EQ (store->online_weight_end (), store->online_weight_begin (transaction));
	ASSERT_EQ (store->online_weight_end (), store->online_weight_rbegin (transaction));
}

TEST (block_store, pruned_blocks)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());

	vban::keypair key1;
	vban::open_block block1 (0, 1, key1.pub, key1.prv, key1.pub, 0);
	auto hash1 (block1.hash ());
	{
		auto transaction (store->tx_begin_write ());

		// Confirm that the store is empty
		ASSERT_FALSE (store->pruned_exists (transaction, hash1));
		ASSERT_EQ (store->pruned_count (transaction), 0);

		// Add one
		store->pruned_put (transaction, hash1);
		ASSERT_TRUE (store->pruned_exists (transaction, hash1));
	}

	// Confirm that it can be found
	ASSERT_EQ (store->pruned_count (store->tx_begin_read ()), 1);

	// Add another one and check that it (and the existing one) can be found
	vban::open_block block2 (1, 2, key1.pub, key1.prv, key1.pub, 0);
	block2.sideband_set ({});
	auto hash2 (block2.hash ());
	{
		auto transaction (store->tx_begin_write ());
		store->pruned_put (transaction, hash2);
		ASSERT_TRUE (store->pruned_exists (transaction, hash2)); // Check new pruned hash is here
		ASSERT_FALSE (store->block_exists (transaction, hash2));
		ASSERT_TRUE (store->pruned_exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block_exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned_count (store->tx_begin_read ()), 2);

	// Delete the first one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned_del (transaction, hash2);
		ASSERT_FALSE (store->pruned_exists (transaction, hash2)); // Confirm it no longer exists
		ASSERT_FALSE (store->block_exists (transaction, hash2)); // true for block_exists
		store->block_put (transaction, hash2, block2); // Add corresponding block
		ASSERT_TRUE (store->block_exists (transaction, hash2));
		ASSERT_TRUE (store->pruned_exists (transaction, hash1)); // Check first pruned hash is still here
		ASSERT_FALSE (store->block_exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned_count (store->tx_begin_read ()), 1);

	// Delete original one
	{
		auto transaction (store->tx_begin_write ());
		store->pruned_del (transaction, hash1);
		ASSERT_FALSE (store->pruned_exists (transaction, hash1));
	}

	ASSERT_EQ (store->pruned_count (store->tx_begin_read ()), 0);
}

TEST (mdb_block_store, upgrade_v14_v15)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	// Extract confirmation height to a separate database
	auto path (vban::unique_path ());
	vban::genesis genesis;
	vban::network_params network_params;
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send (genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	vban::state_block epoch (vban::dev_genesis_key.pub, send.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, network_params.ledger.epochs.link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send.hash ()));
	vban::state_block state_send (vban::dev_genesis_key.pub, epoch.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio * 2, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch.hash ()));
	{
		vban::logger_mt logger;
		vban::mdb_store store (logger, path);
		vban::stat stats;
		vban::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		vban::account_info account_info;
		ASSERT_FALSE (store.account_get (transaction, vban::genesis_account, account_info));
		vban::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store.confirmation_height_get (transaction, vban::genesis_account, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 1);
		ASSERT_EQ (confirmation_height_info.frontier, genesis.hash ());
		// These databases get removed after an upgrade, so readd them
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_v1", MDB_CREATE, &store.state_blocks_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "accounts_v1", MDB_CREATE, &store.accounts_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "pending_v1", MDB_CREATE, &store.pending_v1));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "send", MDB_CREATE, &store.send_blocks));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_blocks", MDB_CREATE, &store.state_blocks));
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send).code);
		// Lower the database to the previous version
		store.version_put (transaction, 14);
		store.confirmation_height_del (transaction, vban::genesis_account);
		modify_account_info_to_v14 (store, transaction, vban::genesis_account, confirmation_height_info.height, state_send.hash ());

		store.pending_del (transaction, vban::pending_key (vban::genesis_account, state_send.hash ()));

		write_sideband_v14 (store, transaction, state_send, store.state_blocks_v1);
		write_sideband_v14 (store, transaction, epoch, store.state_blocks_v1);
		write_block_w_sideband_v18 (store, store.open_blocks, transaction, *genesis.open);
		write_block_w_sideband_v18 (store, store.send_blocks, transaction, send);

		// Remove from blocks table
		store.block_del (transaction, state_send.hash ());
		store.block_del (transaction, epoch.hash ());

		// Turn pending into v14
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v0, vban::mdb_val (vban::pending_key (vban::dev_genesis_key.pub, send.hash ())), vban::mdb_val (vban::pending_info_v14 (vban::genesis_account, vban::Gxrb_ratio, vban::epoch::epoch_0)), 0));
		ASSERT_FALSE (mdb_put (store.env.tx (transaction), store.pending_v1, vban::mdb_val (vban::pending_key (vban::dev_genesis_key.pub, state_send.hash ())), vban::mdb_val (vban::pending_info_v14 (vban::genesis_account, vban::Gxrb_ratio, vban::epoch::epoch_1)), 0));

		// This should fail as sizes are no longer correct for account_info
		vban::mdb_val value;
		ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts_v1, vban::mdb_val (vban::genesis_account), value));
		vban::account_info info;
		ASSERT_NE (value.size (), info.db_size ());
		store.account_del (transaction, vban::genesis_account);

		// Confirmation height for the account should be deleted
		ASSERT_TRUE (mdb_get (store.env.tx (transaction), store.confirmation_height, vban::mdb_val (vban::genesis_account), value));
	}

	// Now do the upgrade
	vban::logger_mt logger;
	vban::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// Size of account_info should now equal that set in db
	vban::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.accounts, vban::mdb_val (vban::genesis_account), value));
	vban::account_info info (value);
	ASSERT_EQ (value.size (), info.db_size ());

	// Confirmation height should exist
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_FALSE (store.confirmation_height_get (transaction, vban::genesis_account, confirmation_height_info));
	ASSERT_EQ (confirmation_height_info.height, 1);
	ASSERT_EQ (confirmation_height_info.frontier, genesis.hash ());

	// accounts_v1, state_blocks_v1 & pending_v1 tables should be deleted
	auto error_get_accounts_v1 (mdb_get (store.env.tx (transaction), store.accounts_v1, vban::mdb_val (vban::genesis_account), value));
	ASSERT_NE (error_get_accounts_v1, MDB_SUCCESS);
	auto error_get_pending_v1 (mdb_get (store.env.tx (transaction), store.pending_v1, vban::mdb_val (vban::pending_key (vban::dev_genesis_key.pub, state_send.hash ())), value));
	ASSERT_NE (error_get_pending_v1, MDB_SUCCESS);
	auto error_get_state_v1 (mdb_get (store.env.tx (transaction), store.state_blocks_v1, vban::mdb_val (state_send.hash ()), value));
	ASSERT_NE (error_get_state_v1, MDB_SUCCESS);

	// Check that the epochs are set correctly for the sideband, accounts and pending entries
	auto block = store.block_get (transaction, state_send.hash ());
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
	block = store.block_get (transaction, send.hash ());
	ASSERT_NE (block, nullptr);
	ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_0);
	ASSERT_EQ (info.epoch (), vban::epoch::epoch_1);
	vban::pending_info pending_info;
	store.pending_get (transaction, vban::pending_key (vban::dev_genesis_key.pub, send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, vban::epoch::epoch_0);
	store.pending_get (transaction, vban::pending_key (vban::dev_genesis_key.pub, state_send.hash ()), pending_info);
	ASSERT_EQ (pending_info.epoch, vban::epoch::epoch_1);

	// Version should be correct
	ASSERT_LT (14, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v15_v16)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (vban::unique_path ());
	vban::mdb_val value;
	{
		vban::genesis genesis;
		vban::logger_mt logger;
		vban::mdb_store store (logger, path);
		vban::stat stats;
		vban::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		// The representation table should get removed after, so readd it so that we can later confirm this actually happens
		auto txn = store.env.tx (transaction);
		ASSERT_FALSE (mdb_dbi_open (txn, "representation", MDB_CREATE, &store.representation));
		auto weight = ledger.cache.rep_weights.representation_get (vban::genesis_account);
		ASSERT_EQ (MDB_SUCCESS, mdb_put (txn, store.representation, vban::mdb_val (vban::genesis_account), vban::mdb_val (vban::uint128_union (weight)), 0));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks));
		write_block_w_sideband_v18 (store, store.open_blocks, transaction, *genesis.open);
		// Lower the database to the previous version
		store.version_put (transaction, 15);
		// Confirm the rep weight exists in the database
		ASSERT_EQ (MDB_SUCCESS, mdb_get (store.env.tx (transaction), store.representation, vban::mdb_val (vban::genesis_account), value));
		store.confirmation_height_del (transaction, vban::genesis_account);
	}

	// Now do the upgrade
	vban::logger_mt logger;
	vban::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// The representation table should now be deleted
	auto error_get_representation (mdb_get (store.env.tx (transaction), store.representation, vban::mdb_val (vban::genesis_account), value));
	ASSERT_NE (MDB_SUCCESS, error_get_representation);
	ASSERT_EQ (store.representation, 0);

	// Version should be correct
	ASSERT_LT (15, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v16_v17)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	vban::genesis genesis;
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::state_block block1 (vban::dev_genesis_key.pub, genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	vban::state_block block2 (vban::dev_genesis_key.pub, block1.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio - 1, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block1.hash ()));
	vban::state_block block3 (vban::dev_genesis_key.pub, block2.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio - 2, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (block2.hash ()));

	auto code = [&block1, &block2, &block3] (auto confirmation_height, vban::block_hash const & expected_cemented_frontier) {
		auto path (vban::unique_path ());
		vban::mdb_val value;
		{
			vban::genesis genesis;
			vban::logger_mt logger;
			vban::mdb_store store (logger, path);
			vban::stat stats;
			vban::ledger ledger (store, stats);
			auto transaction (store.tx_begin_write ());
			store.initialize (transaction, genesis, ledger.cache);
			ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block1).code);
			ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block2).code);
			ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, block3).code);
			modify_confirmation_height_to_v15 (store, transaction, vban::genesis_account, confirmation_height);

			ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks));
			write_block_w_sideband_v18 (store, store.open_blocks, transaction, *genesis.open);
			ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_blocks", MDB_CREATE, &store.state_blocks));
			write_block_w_sideband_v18 (store, store.state_blocks, transaction, block1);
			write_block_w_sideband_v18 (store, store.state_blocks, transaction, block2);
			write_block_w_sideband_v18 (store, store.state_blocks, transaction, block3);

			// Lower the database to the previous version
			store.version_put (transaction, 16);
		}

		// Now do the upgrade
		vban::logger_mt logger;
		vban::mdb_store store (logger, path);
		ASSERT_FALSE (store.init_error ());
		auto transaction (store.tx_begin_read ());

		vban::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store.confirmation_height_get (transaction, vban::genesis_account, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, confirmation_height);

		// Check confirmation height frontier is correct
		ASSERT_EQ (confirmation_height_info.frontier, expected_cemented_frontier);

		// Version should be correct
		ASSERT_LT (16, store.version_get (transaction));
	};

	code (0, vban::block_hash (0));
	code (1, genesis.hash ());
	code (2, block1.hash ());
	code (3, block2.hash ());
	code (4, block3.hash ());
}

TEST (mdb_block_store, upgrade_v17_v18)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (vban::unique_path ());
	vban::genesis genesis;
	vban::keypair key1;
	vban::keypair key2;
	vban::keypair key3;
	vban::network_params network_params;
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::send_block send_zero (genesis.hash (), vban::dev_genesis_key.pub, vban::genesis_amount, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (genesis.hash ()));
	vban::state_block state_receive_zero (vban::dev_genesis_key.pub, send_zero.hash (), vban::dev_genesis_key.pub, vban::genesis_amount, send_zero.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send_zero.hash ()));
	vban::state_block epoch (vban::dev_genesis_key.pub, state_receive_zero.hash (), vban::dev_genesis_key.pub, vban::genesis_amount, network_params.ledger.epochs.link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_receive_zero.hash ()));
	vban::state_block state_send (vban::dev_genesis_key.pub, epoch.hash (), vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (epoch.hash ()));
	vban::state_block state_receive (vban::dev_genesis_key.pub, state_send.hash (), vban::dev_genesis_key.pub, vban::genesis_amount, state_send.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_send.hash ()));
	vban::state_block state_change (vban::dev_genesis_key.pub, state_receive.hash (), vban::dev_genesis_key.pub, vban::genesis_amount, 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_receive.hash ()));
	vban::state_block state_send_change (vban::dev_genesis_key.pub, state_change.hash (), key1.pub, vban::genesis_amount - vban::Gxrb_ratio, key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_change.hash ()));
	vban::state_block epoch_first (key1.pub, 0, 0, 0, network_params.ledger.epochs.link (vban::epoch::epoch_2), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (key1.pub));
	vban::state_block state_receive2 (key1.pub, epoch_first.hash (), key1.pub, vban::Gxrb_ratio, state_send_change.hash (), key1.prv, key1.pub, *pool.generate (epoch_first.hash ()));
	vban::state_block state_send2 (vban::dev_genesis_key.pub, state_send_change.hash (), key1.pub, vban::genesis_amount - vban::Gxrb_ratio * 2, key2.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_send_change.hash ()));
	vban::state_block state_open (key2.pub, 0, key2.pub, vban::Gxrb_ratio, state_send2.hash (), key2.prv, key2.pub, *pool.generate (key2.pub));
	vban::state_block state_send_epoch_link (key2.pub, state_open.hash (), key2.pub, 0, network_params.ledger.epochs.link (vban::epoch::epoch_2), key2.prv, key2.pub, *pool.generate (state_open.hash ()));
	{
		vban::logger_mt logger;
		vban::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		vban::stat stats;
		vban::ledger ledger (store, stats);
		store.initialize (transaction, genesis, ledger.cache);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send_zero).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_receive_zero).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_receive).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_change).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send_change).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, epoch_first).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_receive2).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send2).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_open).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send_epoch_link).code);

		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "open", MDB_CREATE, &store.open_blocks));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "send", MDB_CREATE, &store.send_blocks));
		ASSERT_FALSE (mdb_dbi_open (store.env.tx (transaction), "state_blocks", MDB_CREATE, &store.state_blocks));

		// Downgrade the store
		store.version_put (transaction, 17);

		write_block_w_sideband_v18 (store, store.state_blocks, transaction, state_receive);
		write_block_w_sideband_v18 (store, store.state_blocks, transaction, epoch_first);
		write_block_w_sideband_v18 (store, store.state_blocks, transaction, state_send2);
		write_block_w_sideband_v18 (store, store.state_blocks, transaction, state_send_epoch_link);
		write_block_w_sideband_v18 (store, store.open_blocks, transaction, *genesis.open);
		write_block_w_sideband_v18 (store, store.send_blocks, transaction, send_zero);

		// Replace with the previous sideband version for state blocks
		// The upgrade can resume after upgrading some blocks, test this by only downgrading some of them
		write_sideband_v15 (store, transaction, state_receive_zero);
		write_sideband_v15 (store, transaction, epoch);
		write_sideband_v15 (store, transaction, state_send);
		write_sideband_v15 (store, transaction, state_change);
		write_sideband_v15 (store, transaction, state_send_change);
		write_sideband_v15 (store, transaction, state_receive2);
		write_sideband_v15 (store, transaction, state_open);

		store.block_del (transaction, state_receive_zero.hash ());
		store.block_del (transaction, epoch.hash ());
		store.block_del (transaction, state_send.hash ());
		store.block_del (transaction, state_change.hash ());
		store.block_del (transaction, state_send_change.hash ());
		store.block_del (transaction, state_receive2.hash ());
		store.block_del (transaction, state_open.hash ());
	}

	// Now do the upgrade
	vban::logger_mt logger;
	vban::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// Size of state block should equal that set in db (no change)
	vban::mdb_val value;
	ASSERT_FALSE (mdb_get (store.env.tx (transaction), store.blocks, vban::mdb_val (state_send.hash ()), value));
	ASSERT_EQ (value.size (), sizeof (vban::block_type) + vban::state_block::size + vban::block_sideband::size (vban::block_type::state));

	// Check that sidebands are correctly populated
	{
		// Non-state unaffected
		auto block = store.block_get (transaction, send_zero.hash ());
		ASSERT_NE (block, nullptr);
		// All defaults
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_0);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State receive from old zero send
		auto block = store.block_get (transaction, state_receive_zero.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_0);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// Epoch
		auto block = store.block_get (transaction, epoch.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_TRUE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State send
		auto block = store.block_get (transaction, state_send.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State receive
		auto block = store.block_get (transaction, state_receive.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// State change
		auto block = store.block_get (transaction, state_change.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State send + change
		auto block = store.block_get (transaction, state_send_change.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// Epoch on unopened account
		auto block = store.block_get (transaction, epoch_first.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_2);
		ASSERT_TRUE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State open following epoch
		auto block = store.block_get (transaction, state_receive2.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_2);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// Another state send
		auto block = store.block_get (transaction, state_send2.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	{
		// State open
		auto block = store.block_get (transaction, state_open.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_FALSE (block->sideband ().details.is_send);
		ASSERT_TRUE (block->sideband ().details.is_receive);
	}
	{
		// State send to an epoch link
		auto block = store.block_get (transaction, state_send_epoch_link.hash ());
		ASSERT_NE (block, nullptr);
		ASSERT_EQ (block->sideband ().details.epoch, vban::epoch::epoch_1);
		ASSERT_FALSE (block->sideband ().details.is_epoch);
		ASSERT_TRUE (block->sideband ().details.is_send);
		ASSERT_FALSE (block->sideband ().details.is_receive);
	}
	// Version should be correct
	ASSERT_LT (17, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v18_v19)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (vban::unique_path ());
	vban::keypair key1;
	vban::work_pool pool (std::numeric_limits<unsigned>::max ());
	vban::network_params network_params;
	vban::send_block send (vban::genesis_hash, vban::dev_genesis_key.pub, vban::genesis_amount - vban::Gxrb_ratio, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (vban::genesis_hash));
	vban::receive_block receive (send.hash (), send.hash (), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (send.hash ()));
	vban::change_block change (receive.hash (), 0, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (receive.hash ()));
	vban::state_block state_epoch (vban::dev_genesis_key.pub, change.hash (), 0, vban::genesis_amount, network_params.ledger.epochs.link (vban::epoch::epoch_1), vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (change.hash ()));
	vban::state_block state_send (vban::dev_genesis_key.pub, state_epoch.hash (), 0, vban::genesis_amount - vban::Gxrb_ratio, key1.pub, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (state_epoch.hash ()));
	vban::state_block state_open (key1.pub, 0, 0, vban::Gxrb_ratio, state_send.hash (), key1.prv, key1.pub, *pool.generate (key1.pub));

	{
		vban::genesis genesis;
		vban::logger_mt logger;
		vban::mdb_store store (logger, path);
		vban::stat stats;
		vban::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);

		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, receive).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, change).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_epoch).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_send).code);
		ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, state_open).code);

		// These tables need to be re-opened and populated so that an upgrade can be done
		auto txn = store.env.tx (transaction);
		ASSERT_FALSE (mdb_dbi_open (txn, "open", MDB_CREATE, &store.open_blocks));
		ASSERT_FALSE (mdb_dbi_open (txn, "receive", MDB_CREATE, &store.receive_blocks));
		ASSERT_FALSE (mdb_dbi_open (txn, "send", MDB_CREATE, &store.send_blocks));
		ASSERT_FALSE (mdb_dbi_open (txn, "change", MDB_CREATE, &store.change_blocks));
		ASSERT_FALSE (mdb_dbi_open (txn, "state_blocks", MDB_CREATE, &store.state_blocks));

		// Modify blocks back to the old tables
		write_block_w_sideband_v18 (store, store.open_blocks, transaction, *genesis.open);
		write_block_w_sideband_v18 (store, store.send_blocks, transaction, send);
		write_block_w_sideband_v18 (store, store.receive_blocks, transaction, receive);
		write_block_w_sideband_v18 (store, store.change_blocks, transaction, change);
		write_block_w_sideband_v18 (store, store.state_blocks, transaction, state_epoch);
		write_block_w_sideband_v18 (store, store.state_blocks, transaction, state_send);
		write_block_w_sideband_v18 (store, store.state_blocks, transaction, state_open);

		store.version_put (transaction, 18);
	}

	// Now do the upgrade
	vban::logger_mt logger;
	vban::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());

	// These tables should be deleted
	ASSERT_EQ (store.send_blocks, 0);
	ASSERT_EQ (store.receive_blocks, 0);
	ASSERT_EQ (store.change_blocks, 0);
	ASSERT_EQ (store.open_blocks, 0);
	ASSERT_EQ (store.state_blocks, 0);

	// Confirm these blocks all exist after the upgrade
	ASSERT_TRUE (store.block_get (transaction, send.hash ()));
	ASSERT_TRUE (store.block_get (transaction, receive.hash ()));
	ASSERT_TRUE (store.block_get (transaction, change.hash ()));
	ASSERT_TRUE (store.block_get (transaction, vban::genesis_hash));
	auto state_epoch_disk (store.block_get (transaction, state_epoch.hash ()));
	ASSERT_NE (nullptr, state_epoch_disk);
	ASSERT_EQ (vban::epoch::epoch_1, state_epoch_disk->sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, state_epoch_disk->sideband ().source_epoch); // Not used for epoch state blocks
	ASSERT_TRUE (store.block_get (transaction, state_send.hash ()));
	auto state_send_disk (store.block_get (transaction, state_send.hash ()));
	ASSERT_NE (nullptr, state_send_disk);
	ASSERT_EQ (vban::epoch::epoch_1, state_send_disk->sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_0, state_send_disk->sideband ().source_epoch); // Not used for send state blocks
	ASSERT_TRUE (store.block_get (transaction, state_open.hash ()));
	auto state_open_disk (store.block_get (transaction, state_open.hash ()));
	ASSERT_NE (nullptr, state_open_disk);
	ASSERT_EQ (vban::epoch::epoch_1, state_open_disk->sideband ().details.epoch);
	ASSERT_EQ (vban::epoch::epoch_1, state_open_disk->sideband ().source_epoch);

	ASSERT_EQ (7, store.count (transaction, store.blocks));

	// Version should be correct
	ASSERT_LT (18, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v19_v20)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (vban::unique_path ());
	vban::genesis genesis;
	vban::logger_mt logger;
	vban::stat stats;
	{
		vban::mdb_store store (logger, path);
		vban::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		// Delete pruned table
		ASSERT_FALSE (mdb_drop (store.env.tx (transaction), store.pruned, 1));
		store.version_put (transaction, 19);
	}
	// Upgrading should create the table
	vban::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	ASSERT_NE (store.pruned, 0);

	// Version should be correct
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (19, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_v20_v21)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (vban::unique_path ());
	vban::genesis genesis;
	vban::logger_mt logger;
	vban::stat stats;
	{
		vban::mdb_store store (logger, path);
		vban::ledger ledger (store, stats);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis, ledger.cache);
		// Delete pruned table
		ASSERT_FALSE (mdb_drop (store.env.tx (transaction), store.final_votes, 1));
		store.version_put (transaction, 20);
	}
	// Upgrading should create the table
	vban::mdb_store store (logger, path);
	ASSERT_FALSE (store.init_error ());
	ASSERT_NE (store.final_votes, 0);

	// Version should be correct
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (19, store.version_get (transaction));
}

TEST (mdb_block_store, upgrade_backup)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto dir (vban::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	auto path = dir / "data.ldb";
	/** Returns 'dir' if backup file cannot be found */
	auto get_backup_path = [&dir] () {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("data_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};

	{
		vban::logger_mt logger;
		vban::genesis genesis;
		vban::mdb_store store (logger, path);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 14);
	}
	ASSERT_EQ (get_backup_path ().string (), dir.string ());

	// Now do the upgrade and confirm that backup is saved
	vban::logger_mt logger;
	vban::mdb_store store (logger, path, vban::txn_tracking_config{}, std::chrono::seconds (5), vban::lmdb_config{}, true);
	ASSERT_FALSE (store.init_error ());
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (14, store.version_get (transaction));
	ASSERT_NE (get_backup_path ().string (), dir.string ());
}

// Test various confirmation height values as well as clearing them
TEST (block_store, confirmation_height)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode
		return;
	}
	auto path (vban::unique_path ());
	vban::logger_mt logger;
	auto store = vban::make_store (logger, path);

	vban::account account1 (0);
	vban::account account2 (1);
	vban::account account3 (2);
	vban::block_hash cemented_frontier1 (3);
	vban::block_hash cemented_frontier2 (4);
	vban::block_hash cemented_frontier3 (5);
	{
		auto transaction (store->tx_begin_write ());
		store->confirmation_height_put (transaction, account1, { 500, cemented_frontier1 });
		store->confirmation_height_put (transaction, account2, { std::numeric_limits<uint64_t>::max (), cemented_frontier2 });
		store->confirmation_height_put (transaction, account3, { 10, cemented_frontier3 });

		vban::confirmation_height_info confirmation_height_info;
		ASSERT_FALSE (store->confirmation_height_get (transaction, account1, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 500);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier1);
		ASSERT_FALSE (store->confirmation_height_get (transaction, account2, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, std::numeric_limits<uint64_t>::max ());
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier2);
		ASSERT_FALSE (store->confirmation_height_get (transaction, account3, confirmation_height_info));
		ASSERT_EQ (confirmation_height_info.height, 10);
		ASSERT_EQ (confirmation_height_info.frontier, cemented_frontier3);

		// Check clearing of confirmation heights
		store->confirmation_height_clear (transaction);
	}
	auto transaction (store->tx_begin_read ());
	ASSERT_EQ (store->confirmation_height_count (transaction), 0);
	vban::confirmation_height_info confirmation_height_info;
	ASSERT_TRUE (store->confirmation_height_get (transaction, account1, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height_get (transaction, account2, confirmation_height_info));
	ASSERT_TRUE (store->confirmation_height_get (transaction, account3, confirmation_height_info));
}

// Test various confirmation height values as well as clearing them
TEST (block_store, final_vote)
{
	if (vban::using_rocksdb_in_tests ())
	{
		// Don't test this in rocksdb mode as deletions cause inaccurate counts
		return;
	}
	auto path (vban::unique_path ());
	vban::logger_mt logger;
	auto store = vban::make_store (logger, path);

	{
		auto qualified_root = vban::genesis ().open->qualified_root ();
		auto transaction (store->tx_begin_write ());
		store->final_vote_put (transaction, qualified_root, vban::block_hash (2));
		ASSERT_EQ (store->final_vote_count (transaction), 1);
		store->final_vote_clear (transaction);
		ASSERT_EQ (store->final_vote_count (transaction), 0);
		store->final_vote_put (transaction, qualified_root, vban::block_hash (2));
		ASSERT_EQ (store->final_vote_count (transaction), 1);
		// Clearing with incorrect root shouldn't remove
		store->final_vote_clear (transaction, qualified_root.previous ());
		ASSERT_EQ (store->final_vote_count (transaction), 1);
		// Clearing with correct root should remove
		store->final_vote_clear (transaction, qualified_root.root ());
		ASSERT_EQ (store->final_vote_count (transaction), 0);
	}
}

// Ledger versions are not forward compatible
TEST (block_store, incompatible_version)
{
	auto path (vban::unique_path ());
	vban::logger_mt logger;
	{
		auto store = vban::make_store (logger, path);
		ASSERT_FALSE (store->init_error ());

		// Put version to an unreachable number so that it should always be incompatible
		auto transaction (store->tx_begin_write ());
		store->version_put (transaction, std::numeric_limits<int>::max ());
	}

	// Now try and read it, should give an error
	{
		auto store = vban::make_store (logger, path, true);
		ASSERT_TRUE (store->init_error ());

		auto transaction = store->tx_begin_read ();
		auto version_l = store->version_get (transaction);
		ASSERT_EQ (version_l, std::numeric_limits<int>::max ());
	}
}

TEST (block_store, reset_renew_existing_transaction)
{
	vban::logger_mt logger;
	auto store = vban::make_store (logger, vban::unique_path ());
	ASSERT_TRUE (!store->init_error ());

	vban::keypair key1;
	vban::open_block block (0, 1, 1, vban::keypair ().prv, 0, 0);
	block.sideband_set ({});
	auto hash1 (block.hash ());
	auto read_transaction = store->tx_begin_read ();

	// Block shouldn't exist yet
	auto block_non_existing (store->block_get (read_transaction, hash1));
	ASSERT_EQ (nullptr, block_non_existing);

	// Release resources for the transaction
	read_transaction.reset ();

	// Write the block
	{
		auto write_transaction (store->tx_begin_write ());
		store->block_put (write_transaction, hash1, block);
	}

	read_transaction.renew ();

	// Block should exist now
	auto block_existing (store->block_get (read_transaction, hash1));
	ASSERT_NE (nullptr, block_existing);
}

TEST (block_store, rocksdb_force_test_env_variable)
{
	vban::logger_mt logger;

	// Set environment variable
	constexpr auto env_var = "TEST_USE_ROCKSDB";
	auto value = std::getenv (env_var);

	auto store = vban::make_store (logger, vban::unique_path ());

	auto mdb_cast = dynamic_cast<vban::mdb_store *> (store.get ());
	if (value && boost::lexical_cast<int> (value) == 1)
	{
		ASSERT_NE (boost::polymorphic_downcast<vban::rocksdb_store *> (store.get ()), nullptr);
	}
	else
	{
		ASSERT_NE (mdb_cast, nullptr);
	}
}

namespace vban
{
TEST (rocksdb_block_store, tombstone_count)
{
	if (vban::using_rocksdb_in_tests ())
	{
		vban::logger_mt logger;
		auto store = std::make_unique<vban::rocksdb_store> (logger, vban::unique_path ());
		ASSERT_TRUE (!store->init_error ());
		auto transaction = store->tx_begin_write ();
		auto block1 (std::make_shared<vban::send_block> (0, 1, 2, vban::keypair ().prv, 4, 5));
		store->unchecked_put (transaction, block1->previous (), block1);
		ASSERT_EQ (store->tombstone_map.at (vban::tables::unchecked).num_since_last_flush.load (), 0);
		store->unchecked_del (transaction, vban::unchecked_key (block1->previous (), block1->hash ()));
		ASSERT_EQ (store->tombstone_map.at (vban::tables::unchecked).num_since_last_flush.load (), 1);
	}
}
}

namespace
{
void write_sideband_v14 (vban::mdb_store & store_a, vban::transaction & transaction_a, vban::block const & block_a, MDB_dbi db_a)
{
	auto block = store_a.block_get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);

	vban::block_sideband_v14 sideband_v14 (block->type (), block->sideband ().account, block->sideband ().successor, block->sideband ().balance, block->sideband ().timestamp, block->sideband ().height);
	std::vector<uint8_t> data;
	{
		vban::vectorstream stream (data);
		block_a.serialize (stream);
		sideband_v14.serialize (stream);
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), block->sideband ().details.epoch == vban::epoch::epoch_0 ? store_a.state_blocks_v0 : store_a.state_blocks_v1, vban::mdb_val (block_a.hash ()), &val, 0));
}

void write_sideband_v15 (vban::mdb_store & store_a, vban::transaction & transaction_a, vban::block const & block_a)
{
	auto block = store_a.block_get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);

	ASSERT_LE (block->sideband ().details.epoch, vban::epoch::max);
	// Simulated by writing 0 on every of the most significant bits, leaving out epoch only, as if pre-upgrade
	vban::block_sideband_v18 sideband_v15 (block->sideband ().account, block->sideband ().successor, block->sideband ().balance, block->sideband ().timestamp, block->sideband ().height, block->sideband ().details.epoch, false, false, false);
	std::vector<uint8_t> data;
	{
		vban::vectorstream stream (data);
		block_a.serialize (stream);
		sideband_v15.serialize (stream, block_a.type ());
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), store_a.state_blocks, vban::mdb_val (block_a.hash ()), &val, 0));
}

void write_block_w_sideband_v18 (vban::mdb_store & store_a, MDB_dbi database, vban::write_transaction & transaction_a, vban::block const & block_a)
{
	auto block = store_a.block_get (transaction_a, block_a.hash ());
	ASSERT_NE (block, nullptr);
	auto new_sideband (block->sideband ());
	vban::block_sideband_v18 sideband_v18 (new_sideband.account, new_sideband.successor, new_sideband.balance, new_sideband.height, new_sideband.timestamp, new_sideband.details.epoch, new_sideband.details.is_send, new_sideband.details.is_receive, new_sideband.details.is_epoch);

	std::vector<uint8_t> data;
	{
		vban::vectorstream stream (data);
		block->serialize (stream);
		sideband_v18.serialize (stream, block->type ());
	}

	MDB_val val{ data.size (), data.data () };
	ASSERT_FALSE (mdb_put (store_a.env.tx (transaction_a), database, vban::mdb_val (block_a.hash ()), &val, 0));
	store_a.del (transaction_a, vban::tables::blocks, vban::mdb_val (block_a.hash ()));
}

void modify_account_info_to_v14 (vban::mdb_store & store, vban::transaction const & transaction, vban::account const & account, uint64_t confirmation_height, vban::block_hash const & rep_block)
{
	vban::account_info info;
	ASSERT_FALSE (store.account_get (transaction, account, info));
	vban::account_info_v14 account_info_v14 (info.head, rep_block, info.open_block, info.balance, info.modified, info.block_count, confirmation_height, info.epoch ());
	auto status (mdb_put (store.env.tx (transaction), info.epoch () == vban::epoch::epoch_0 ? store.accounts_v0 : store.accounts_v1, vban::mdb_val (account), vban::mdb_val (account_info_v14), 0));
	ASSERT_EQ (status, 0);
}

void modify_confirmation_height_to_v15 (vban::mdb_store & store, vban::transaction const & transaction, vban::account const & account, uint64_t confirmation_height)
{
	auto status (mdb_put (store.env.tx (transaction), store.confirmation_height, vban::mdb_val (account), vban::mdb_val (confirmation_height), 0));
	ASSERT_EQ (status, 0);
}
}
