#pragma once

#include <vban/lib/errors.hpp>
#include <vban/node/node_pow_server_config.hpp>
#include <vban/node/node_rpc_config.hpp>
#include <vban/node/nodeconfig.hpp>
#include <vban/node/openclconfig.hpp>

#include <vector>

namespace vban
{
class jsonconfig;
class tomlconfig;
class daemon_config
{
public:
	daemon_config () = default;
	daemon_config (boost::filesystem::path const & data_path);
	vban::error deserialize_json (bool &, vban::jsonconfig &);
	vban::error serialize_json (vban::jsonconfig &);
	vban::error deserialize_toml (vban::tomlconfig &);
	vban::error serialize_toml (vban::tomlconfig &);
	bool rpc_enable{ false };
	vban::node_rpc_config rpc;
	vban::node_config node;
	bool opencl_enable{ false };
	vban::opencl_config opencl;
	vban::node_pow_server_config pow_server;
	boost::filesystem::path data_path;
	unsigned json_version () const
	{
		return 2;
	}
};

vban::error read_node_config_toml (boost::filesystem::path const &, vban::daemon_config & config_a, std::vector<std::string> const & config_overrides = std::vector<std::string> ());
vban::error read_and_update_daemon_config (boost::filesystem::path const &, vban::daemon_config & config_a, vban::jsonconfig & json_a);
}
