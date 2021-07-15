#include <vban/lib/config.hpp>
#include <vban/lib/jsonconfig.hpp>
#include <vban/lib/tomlconfig.hpp>
#include <vban/node/node_rpc_config.hpp>

#include <boost/property_tree/ptree.hpp>

vban::error vban::node_rpc_config::serialize_json (vban::jsonconfig & json) const
{
	json.put ("version", json_version ());
	json.put ("enable_sign_hash", enable_sign_hash);

	vban::jsonconfig child_process_l;
	child_process_l.put ("enable", child_process.enable);
	child_process_l.put ("rpc_path", child_process.rpc_path);
	json.put_child ("child_process", child_process_l);
	return json.get_error ();
}

vban::error vban::node_rpc_config::serialize_toml (vban::tomlconfig & toml) const
{
	toml.put ("enable_sign_hash", enable_sign_hash, "Allow or disallow signing of hashes.\ntype:bool");

	vban::tomlconfig child_process_l;
	child_process_l.put ("enable", child_process.enable, "Enable or disable RPC child process. If false, an in-process RPC server is used.\ntype:bool");
	child_process_l.put ("rpc_path", child_process.rpc_path, "Path to the vban_rpc executable. Must be set if child process is enabled.\ntype:string,path");
	toml.put_child ("child_process", child_process_l);
	return toml.get_error ();
}

vban::error vban::node_rpc_config::deserialize_toml (vban::tomlconfig & toml)
{
	toml.get_optional ("enable_sign_hash", enable_sign_hash);
	toml.get_optional<bool> ("enable_sign_hash", enable_sign_hash);

	auto child_process_l (toml.get_optional_child ("child_process"));
	if (child_process_l)
	{
		child_process_l->get_optional<bool> ("enable", child_process.enable);
		child_process_l->get_optional<std::string> ("rpc_path", child_process.rpc_path);
	}

	return toml.get_error ();
}

vban::error vban::node_rpc_config::deserialize_json (bool & upgraded_a, vban::jsonconfig & json, boost::filesystem::path const & data_path)
{
	json.get_optional<bool> ("enable_sign_hash", enable_sign_hash);

	auto child_process_l (json.get_optional_child ("child_process"));
	if (child_process_l)
	{
		child_process_l->get_optional<bool> ("enable", child_process.enable);
		child_process_l->get_optional<std::string> ("rpc_path", child_process.rpc_path);
	}

	return json.get_error ();
}

void vban::node_rpc_config::set_request_callback (std::function<void (boost::property_tree::ptree const &)> callback_a)
{
	debug_assert (vban::network_constants ().is_dev_network ());
	request_callback = std::move (callback_a);
}
