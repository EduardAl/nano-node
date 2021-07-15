#pragma once

#include <vban/ipc_flatbuffers_lib/generated/flatbuffers/vbanapi_generated.h>
#include <vban/lib/errors.hpp>
#include <vban/lib/ipc.hpp>
#include <vban/node/ipc/ipc_access_config.hpp>
#include <vban/node/ipc/ipc_broker.hpp>
#include <vban/node/node_rpc_config.hpp>

#include <atomic>
#include <memory>
#include <mutex>

namespace flatbuffers
{
class Parser;
}
namespace vban
{
class node;
class error;
namespace ipc
{
	class access;
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server final
	{
	public:
		ipc_server (vban::node & node, vban::node_rpc_config const & node_rpc_config);
		~ipc_server ();
		void stop ();

		vban::node & node;
		vban::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 1 };
		std::shared_ptr<vban::ipc::broker> get_broker ();
		vban::ipc::access & get_access ();
		vban::error reload_access_config ();

	private:
		void setup_callbacks ();
		std::shared_ptr<vban::ipc::broker> broker;
		vban::ipc::access access;
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<vban::ipc::transport>> transports;
	};
}
}
