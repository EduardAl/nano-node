#include <vban/lib/stats.hpp>
#include <vban/lib/work.hpp>
#include <vban/secure/blockstore.hpp>
#include <vban/secure/ledger.hpp>
#include <vban/secure/utility.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (processor_service, bad_send_signature)
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
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::keypair key2;
	vban::send_block send (info1.head, vban::dev_genesis_key.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	send.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (vban::process_result::bad_signature, ledger.process (transaction, send).code);
}

TEST (processor_service, bad_receive_signature)
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
	vban::account_info info1;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info1));
	vban::send_block send (info1.head, vban::dev_genesis_key.pub, 50, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (info1.head));
	vban::block_hash hash1 (send.hash ());
	ASSERT_EQ (vban::process_result::progress, ledger.process (transaction, send).code);
	vban::account_info info2;
	ASSERT_FALSE (store->account_get (transaction, vban::dev_genesis_key.pub, info2));
	vban::receive_block receive (hash1, hash1, vban::dev_genesis_key.prv, vban::dev_genesis_key.pub, *pool.generate (hash1));
	receive.signature.bytes[32] ^= 0x1;
	ASSERT_EQ (vban::process_result::bad_signature, ledger.process (transaction, receive).code);
}
