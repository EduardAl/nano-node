#include <vban/node/common.hpp>
#include <vban/node/testing.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace vban
{
void force_vban_dev_network ();
}
namespace
{
std::shared_ptr<vban::system> system0;
std::shared_ptr<vban::node> node0;

class fuzz_visitor : public vban::message_visitor
{
public:
	virtual void keepalive (vban::keepalive const &) override
	{
	}
	virtual void publish (vban::publish const &) override
	{
	}
	virtual void confirm_req (vban::confirm_req const &) override
	{
	}
	virtual void confirm_ack (vban::confirm_ack const &) override
	{
	}
	virtual void bulk_pull (vban::bulk_pull const &) override
	{
	}
	virtual void bulk_pull_account (vban::bulk_pull_account const &) override
	{
	}
	virtual void bulk_push (vban::bulk_push const &) override
	{
	}
	virtual void frontier_req (vban::frontier_req const &) override
	{
	}
	virtual void node_id_handshake (vban::node_id_handshake const &) override
	{
	}
	virtual void telemetry_req (vban::telemetry_req const &) override
	{
	}
	virtual void telemetry_ack (vban::telemetry_ack const &) override
	{
	}
};
}

/** Fuzz live message parsing. This covers parsing and block/vote uniquing. */
void fuzz_message_parser (const uint8_t * Data, size_t Size)
{
	static bool initialized = false;
	if (!initialized)
	{
		vban::force_vban_dev_network ();
		initialized = true;
		system0 = std::make_shared<vban::system> (1);
		node0 = system0->nodes[0];
	}

	fuzz_visitor visitor;
	vban::message_parser parser (node0->network.publish_filter, node0->block_uniquer, node0->vote_uniquer, visitor, node0->work);
	parser.deserialize_buffer (Data, Size);
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * Data, size_t Size)
{
	fuzz_message_parser (Data, Size);
	return 0;
}
