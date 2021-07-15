#pragma once

#include <vban/boost/asio/ip/tcp.hpp>
#include <vban/lib/logger_mt.hpp>
#include <vban/lib/rpc_handler_interface.hpp>
#include <vban/lib/rpcconfig.hpp>

namespace boost
{
namespace asio
{
	class io_context;
}
}

namespace vban
{
class rpc_handler_interface;

class rpc
{
public:
	rpc (boost::asio::io_context & io_ctx_a, vban::rpc_config const & config_a, vban::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();
	void start ();
	virtual void accept ();
	void stop ();

	vban::rpc_config config;
	boost::asio::ip::tcp::acceptor acceptor;
	vban::logger_mt logger;
	boost::asio::io_context & io_ctx;
	vban::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<vban::rpc> get_rpc (boost::asio::io_context & io_ctx_a, vban::rpc_config const & config_a, vban::rpc_handler_interface & rpc_handler_interface_a);
}
