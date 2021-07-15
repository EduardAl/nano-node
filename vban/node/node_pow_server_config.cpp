#include <vban/lib/tomlconfig.hpp>
#include <vban/node/node_pow_server_config.hpp>

vban::error vban::node_pow_server_config::serialize_toml (vban::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Value is currently not in use. Enable or disable starting Vban PoW Server as a child process.\ntype:bool");
	toml.put ("vban_pow_server_path", pow_server_path, "Value is currently not in use. Path to the vban_pow_server executable.\ntype:string,path");
	return toml.get_error ();
}

vban::error vban::node_pow_server_config::deserialize_toml (vban::tomlconfig & toml)
{
	toml.get_optional<bool> ("enable", enable);
	toml.get_optional<std::string> ("vban_pow_server_path", pow_server_path);

	return toml.get_error ();
}
