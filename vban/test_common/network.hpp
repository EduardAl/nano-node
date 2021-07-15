#pragma once

#include <vban/node/common.hpp>

namespace vban
{
class node;
class system;

namespace transport
{
	class channel_tcp;
}

/** Waits until a TCP connection is established and returns the TCP channel on success*/
std::shared_ptr<vban::transport::channel_tcp> establish_tcp (vban::system &, vban::node &, vban::endpoint const &);

/** Returns a callback to be used for start_tcp to send a keepalive*/
std::function<void (std::shared_ptr<vban::transport::channel> channel_a)> keepalive_tcp_callback (vban::node &);
}
