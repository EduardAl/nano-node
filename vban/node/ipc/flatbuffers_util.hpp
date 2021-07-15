#pragma once

#include <vban/ipc_flatbuffers_lib/generated/flatbuffers/vbanapi_generated.h>

#include <memory>

namespace vban
{
class amount;
class block;
class send_block;
class receive_block;
class change_block;
class open_block;
class state_block;
namespace ipc
{
	/**
	 * Utilities to convert between blocks and Flatbuffers equivalents
	 */
	class flatbuffers_builder
	{
	public:
		static vbanapi::BlockUnion block_to_union (vban::block const & block_a, vban::amount const & amount_a, bool is_state_send_a = false);
		static std::unique_ptr<vbanapi::BlockStateT> from (vban::state_block const & block_a, vban::amount const & amount_a, bool is_state_send_a);
		static std::unique_ptr<vbanapi::BlockSendT> from (vban::send_block const & block_a);
		static std::unique_ptr<vbanapi::BlockReceiveT> from (vban::receive_block const & block_a);
		static std::unique_ptr<vbanapi::BlockOpenT> from (vban::open_block const & block_a);
		static std::unique_ptr<vbanapi::BlockChangeT> from (vban::change_block const & block_a);
	};
}
}
