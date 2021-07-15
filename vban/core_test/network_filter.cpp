#include <vban/node/common.hpp>
#include <vban/secure/buffer.hpp>
#include <vban/secure/common.hpp>
#include <vban/secure/network_filter.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

TEST (network_filter, unit)
{
	vban::genesis genesis;
	vban::network_filter filter (1);
	auto one_block = [&filter] (std::shared_ptr<vban::block> const & block_a, bool expect_duplicate_a) {
		vban::publish message (block_a);
		auto bytes (message.to_bytes ());
		vban::bufferstream stream (bytes->data (), bytes->size ());

		// First read the header
		bool error{ false };
		vban::message_header header (error, stream);
		ASSERT_FALSE (error);

		// This validates vban::message_header::size
		ASSERT_EQ (bytes->size (), block_a->size (block_a->type ()) + header.size);

		// Now filter the rest of the stream
		bool duplicate (filter.apply (bytes->data (), bytes->size () - header.size));
		ASSERT_EQ (expect_duplicate_a, duplicate);

		// Make sure the stream was rewinded correctly
		auto block (vban::deserialize_block (stream, header.block_type ()));
		ASSERT_NE (nullptr, block);
		ASSERT_EQ (*block, *block_a);
	};
	one_block (genesis.open, false);
	for (int i = 0; i < 10; ++i)
	{
		one_block (genesis.open, true);
	}
	vban::state_block_builder builder;
	auto new_block = builder
					 .account (vban::dev_genesis_key.pub)
					 .previous (genesis.open->hash ())
					 .representative (vban::dev_genesis_key.pub)
					 .balance (vban::genesis_amount - 10 * vban::xrb_ratio)
					 .link (vban::public_key ())
					 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
					 .work (0)
					 .build_shared ();

	one_block (new_block, false);
	for (int i = 0; i < 10; ++i)
	{
		one_block (new_block, true);
	}
	for (int i = 0; i < 100; ++i)
	{
		one_block (genesis.open, false);
		one_block (new_block, false);
	}
}

TEST (network_filter, many)
{
	vban::genesis genesis;
	vban::network_filter filter (4);
	vban::keypair key1;
	for (int i = 0; i < 100; ++i)
	{
		vban::state_block_builder builder;
		auto block = builder
					 .account (vban::dev_genesis_key.pub)
					 .previous (genesis.open->hash ())
					 .representative (vban::dev_genesis_key.pub)
					 .balance (vban::genesis_amount - i * 10 * vban::xrb_ratio)
					 .link (key1.pub)
					 .sign (vban::dev_genesis_key.prv, vban::dev_genesis_key.pub)
					 .work (0)
					 .build_shared ();

		vban::publish message (block);
		auto bytes (message.to_bytes ());
		vban::bufferstream stream (bytes->data (), bytes->size ());

		// First read the header
		bool error{ false };
		vban::message_header header (error, stream);
		ASSERT_FALSE (error);

		// This validates vban::message_header::size
		ASSERT_EQ (bytes->size (), block->size + header.size);

		// Now filter the rest of the stream
		// All blocks should pass through
		ASSERT_FALSE (filter.apply (bytes->data (), block->size));
		ASSERT_FALSE (error);

		// Make sure the stream was rewinded correctly
		auto deserialized_block (vban::deserialize_block (stream, header.block_type ()));
		ASSERT_NE (nullptr, deserialized_block);
		ASSERT_EQ (*block, *deserialized_block);
	}
}

TEST (network_filter, clear)
{
	vban::network_filter filter (1);
	std::vector<uint8_t> bytes1{ 1, 2, 3 };
	std::vector<uint8_t> bytes2{ 1 };
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (bytes1.data (), bytes1.size ());
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (bytes2.data (), bytes2.size ());
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	ASSERT_FALSE (filter.apply (bytes2.data (), bytes2.size ()));
}

TEST (network_filter, optional_digest)
{
	vban::network_filter filter (1);
	std::vector<uint8_t> bytes1{ 1, 2, 3 };
	vban::uint256_t digest{ 0 };
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size (), &digest));
	ASSERT_NE (0, digest);
	ASSERT_TRUE (filter.apply (bytes1.data (), bytes1.size ()));
	filter.clear (digest);
	ASSERT_FALSE (filter.apply (bytes1.data (), bytes1.size ()));
}
