#include <vban/node/testing.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

namespace
{
class dev_visitor : public vban::message_visitor
{
public:
	void keepalive (vban::keepalive const &) override
	{
		++keepalive_count;
	}
	void publish (vban::publish const &) override
	{
		++publish_count;
	}
	void confirm_req (vban::confirm_req const &) override
	{
		++confirm_req_count;
	}
	void confirm_ack (vban::confirm_ack const &) override
	{
		++confirm_ack_count;
	}
	void bulk_pull (vban::bulk_pull const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_pull_account (vban::bulk_pull_account const &) override
	{
		ASSERT_FALSE (true);
	}
	void bulk_push (vban::bulk_push const &) override
	{
		ASSERT_FALSE (true);
	}
	void frontier_req (vban::frontier_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void node_id_handshake (vban::node_id_handshake const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_req (vban::telemetry_req const &) override
	{
		ASSERT_FALSE (true);
	}
	void telemetry_ack (vban::telemetry_ack const &) override
	{
		ASSERT_FALSE (true);
	}

	uint64_t keepalive_count{ 0 };
	uint64_t publish_count{ 0 };
	uint64_t confirm_req_count{ 0 };
	uint64_t confirm_ack_count{ 0 };
};
}

TEST (message_parser, exact_confirm_ack_size)
{
	vban::system system (1);
	dev_visitor visitor;
	vban::network_filter filter (1);
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer vote_uniquer (block_uniquer);
	vban::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work);
	auto block (std::make_shared<vban::send_block> (1, 1, 2, vban::keypair ().prv, 4, *system.work.generate (vban::root (1))));
	auto vote (std::make_shared<vban::vote> (0, vban::keypair ().prv, 0, std::move (block)));
	vban::confirm_ack message (vote);
	std::vector<uint8_t> bytes;
	{
		vban::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	auto error (false);
	vban::bufferstream stream1 (bytes.data (), bytes.size ());
	vban::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	bytes.push_back (0);
	vban::bufferstream stream2 (bytes.data (), bytes.size ());
	vban::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_ack (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_ack_count);
	ASSERT_NE (parser.status, vban::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_size)
{
	vban::system system (1);
	dev_visitor visitor;
	vban::network_filter filter (1);
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer vote_uniquer (block_uniquer);
	vban::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work);
	auto block (std::make_shared<vban::send_block> (1, 1, 2, vban::keypair ().prv, 4, *system.work.generate (vban::root (1))));
	vban::confirm_req message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		vban::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	auto error (false);
	vban::bufferstream stream1 (bytes.data (), bytes.size ());
	vban::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	bytes.push_back (0);
	vban::bufferstream stream2 (bytes.data (), bytes.size ());
	vban::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, vban::message_parser::parse_status::success);
}

TEST (message_parser, exact_confirm_req_hash_size)
{
	vban::system system (1);
	dev_visitor visitor;
	vban::network_filter filter (1);
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer vote_uniquer (block_uniquer);
	vban::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work);
	vban::send_block block (1, 1, 2, vban::keypair ().prv, 4, *system.work.generate (vban::root (1)));
	vban::confirm_req message (block.hash (), block.root ());
	std::vector<uint8_t> bytes;
	{
		vban::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	auto error (false);
	vban::bufferstream stream1 (bytes.data (), bytes.size ());
	vban::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream1, header1);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	bytes.push_back (0);
	vban::bufferstream stream2 (bytes.data (), bytes.size ());
	vban::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_confirm_req (stream2, header2);
	ASSERT_EQ (1, visitor.confirm_req_count);
	ASSERT_NE (parser.status, vban::message_parser::parse_status::success);
}

TEST (message_parser, exact_publish_size)
{
	vban::system system (1);
	dev_visitor visitor;
	vban::network_filter filter (1);
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer vote_uniquer (block_uniquer);
	vban::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work);
	auto block (std::make_shared<vban::send_block> (1, 1, 2, vban::keypair ().prv, 4, *system.work.generate (vban::root (1))));
	vban::publish message (std::move (block));
	std::vector<uint8_t> bytes;
	{
		vban::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.publish_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	auto error (false);
	vban::bufferstream stream1 (bytes.data (), bytes.size ());
	vban::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream1, header1);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	bytes.push_back (0);
	vban::bufferstream stream2 (bytes.data (), bytes.size ());
	vban::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_publish (stream2, header2);
	ASSERT_EQ (1, visitor.publish_count);
	ASSERT_NE (parser.status, vban::message_parser::parse_status::success);
}

TEST (message_parser, exact_keepalive_size)
{
	vban::system system (1);
	dev_visitor visitor;
	vban::network_filter filter (1);
	vban::block_uniquer block_uniquer;
	vban::vote_uniquer vote_uniquer (block_uniquer);
	vban::message_parser parser (filter, block_uniquer, vote_uniquer, visitor, system.work);
	vban::keepalive message;
	std::vector<uint8_t> bytes;
	{
		vban::vectorstream stream (bytes);
		message.serialize (stream);
	}
	ASSERT_EQ (0, visitor.keepalive_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	auto error (false);
	vban::bufferstream stream1 (bytes.data (), bytes.size ());
	vban::message_header header1 (error, stream1);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream1, header1);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_EQ (parser.status, vban::message_parser::parse_status::success);
	bytes.push_back (0);
	vban::bufferstream stream2 (bytes.data (), bytes.size ());
	vban::message_header header2 (error, stream2);
	ASSERT_FALSE (error);
	parser.deserialize_keepalive (stream2, header2);
	ASSERT_EQ (1, visitor.keepalive_count);
	ASSERT_NE (parser.status, vban::message_parser::parse_status::success);
}
