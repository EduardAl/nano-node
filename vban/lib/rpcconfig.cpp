#include <vban/boost/asio/ip/address_v6.hpp>
#include <vban/lib/config.hpp>
#include <vban/lib/jsonconfig.hpp>
#include <vban/lib/rpcconfig.hpp>
#include <vban/lib/tomlconfig.hpp>

#include <boost/dll/runtime_symbol_info.hpp>

vban::error vban::rpc_secure_config::serialize_json (vban::jsonconfig & json) const
{
	json.put ("enable", enable);
	json.put ("verbose_logging", verbose_logging);
	json.put ("server_key_passphrase", server_key_passphrase);
	json.put ("server_cert_path", server_cert_path);
	json.put ("server_key_path", server_key_path);
	json.put ("server_dh_path", server_dh_path);
	json.put ("client_certs_path", client_certs_path);
	return json.get_error ();
}

vban::error vban::rpc_secure_config::deserialize_json (vban::jsonconfig & json)
{
	json.get_required<bool> ("enable", enable);
	json.get_required<bool> ("verbose_logging", verbose_logging);
	json.get_required<std::string> ("server_key_passphrase", server_key_passphrase);
	json.get_required<std::string> ("server_cert_path", server_cert_path);
	json.get_required<std::string> ("server_key_path", server_key_path);
	json.get_required<std::string> ("server_dh_path", server_dh_path);
	json.get_required<std::string> ("client_certs_path", client_certs_path);
	return json.get_error ();
}

vban::error vban::rpc_secure_config::serialize_toml (vban::tomlconfig & toml) const
{
	toml.put ("enable", enable, "Enable or disable TLS support.\ntype:bool");
	toml.put ("verbose_logging", verbose_logging, "Enable or disable verbose logging.\ntype:bool");
	toml.put ("server_key_passphrase", server_key_passphrase, "Server key passphrase.\ntype:string");
	toml.put ("server_cert_path", server_cert_path, "Directory containing certificates.\ntype:string,path");
	toml.put ("server_key_path", server_key_path, "Path to server key PEM file.\ntype:string,path");
	toml.put ("server_dh_path", server_dh_path, "Path to Diffie-Hellman params file.\ntype:string,path");
	toml.put ("client_certs_path", client_certs_path, "Directory containing client certificates.\ntype:string");
	return toml.get_error ();
}

vban::error vban::rpc_secure_config::deserialize_toml (vban::tomlconfig & toml)
{
	toml.get<bool> ("enable", enable);
	toml.get<bool> ("verbose_logging", verbose_logging);
	toml.get<std::string> ("server_key_passphrase", server_key_passphrase);
	toml.get<std::string> ("server_cert_path", server_cert_path);
	toml.get<std::string> ("server_key_path", server_key_path);
	toml.get<std::string> ("server_dh_path", server_dh_path);
	toml.get<std::string> ("client_certs_path", client_certs_path);
	return toml.get_error ();
}

vban::rpc_config::rpc_config () :
	address (boost::asio::ip::address_v6::loopback ().to_string ())
{
}

vban::rpc_config::rpc_config (uint16_t port_a, bool enable_control_a) :
	address (boost::asio::ip::address_v6::loopback ().to_string ()),
	port (port_a),
	enable_control (enable_control_a)
{
}

vban::error vban::rpc_config::serialize_json (vban::jsonconfig & json) const
{
	json.put ("version", json_version ());
	json.put ("address", address);
	json.put ("port", port);
	json.put ("enable_control", enable_control);
	json.put ("max_json_depth", max_json_depth);
	json.put ("max_request_size", max_request_size);

	vban::jsonconfig rpc_process_l;
	rpc_process_l.put ("version", rpc_process.json_version ());
	rpc_process_l.put ("io_threads", rpc_process.io_threads);
	rpc_process_l.put ("ipc_address", rpc_process.ipc_address);
	rpc_process_l.put ("ipc_port", rpc_process.ipc_port);
	rpc_process_l.put ("num_ipc_connections", rpc_process.num_ipc_connections);
	json.put_child ("process", rpc_process_l);
	return json.get_error ();
}

vban::error vban::rpc_config::deserialize_json (bool & upgraded_a, vban::jsonconfig & json)
{
	if (!json.empty ())
	{
		auto rpc_secure_l (json.get_optional_child ("secure"));
		if (rpc_secure_l)
		{
			secure.deserialize_json (*rpc_secure_l);
		}

		boost::asio::ip::address_v6 address_l;
		json.get_required<boost::asio::ip::address_v6> ("address", address_l, boost::asio::ip::address_v6::loopback ());
		address = address_l.to_string ();
		json.get_optional<uint16_t> ("port", port);
		json.get_optional<bool> ("enable_control", enable_control);
		json.get_optional<uint8_t> ("max_json_depth", max_json_depth);
		json.get_optional<uint64_t> ("max_request_size", max_request_size);

		auto rpc_process_l (json.get_optional_child ("process"));
		if (rpc_process_l)
		{
			rpc_process_l->get_optional<unsigned> ("io_threads", rpc_process.io_threads);
			rpc_process_l->get_optional<uint16_t> ("ipc_port", rpc_process.ipc_port);
			boost::asio::ip::address_v6 ipc_address_l;
			rpc_process_l->get_optional<boost::asio::ip::address_v6> ("ipc_address", ipc_address_l);
			rpc_process.ipc_address = ipc_address_l.to_string ();
			rpc_process_l->get_optional<unsigned> ("num_ipc_connections", rpc_process.num_ipc_connections);
		}
	}
	else
	{
		upgraded_a = true;
		serialize_json (json);
	}

	return json.get_error ();
}

vban::error vban::rpc_config::serialize_toml (vban::tomlconfig & toml) const
{
	toml.put ("address", address, "Bind address for the RPC server.\ntype:string,ip");
	toml.put ("port", port, "Listening port for the RPC server.\ntype:uint16");
	toml.put ("enable_control", enable_control, "Enable or disable control-level requests.\nWARNING: Enabling this gives anyone with RPC access the ability to stop the node and access wallet funds.\ntype:bool");
	toml.put ("max_json_depth", max_json_depth, "Maximum number of levels in JSON requests.\ntype:uint8");
	toml.put ("max_request_size", max_request_size, "Maximum number of bytes allowed in request bodies.\ntype:uint64");

	vban::tomlconfig rpc_process_l;
	rpc_process_l.put ("io_threads", rpc_process.io_threads, "Number of threads used to serve IO.\ntype:uint32");
	rpc_process_l.put ("ipc_address", rpc_process.ipc_address, "Address of IPC server.\ntype:string,ip");
	rpc_process_l.put ("ipc_port", rpc_process.ipc_port, "Listening port of IPC server.\ntype:uint16");
	rpc_process_l.put ("num_ipc_connections", rpc_process.num_ipc_connections, "Number of IPC connections to establish.\ntype:uint32");
	toml.put_child ("process", rpc_process_l);

	vban::tomlconfig rpc_logging_l;
	rpc_logging_l.put ("log_rpc", rpc_logging.log_rpc, "Whether to log RPC calls.\ntype:bool");
	toml.put_child ("logging", rpc_logging_l);
	return toml.get_error ();
}

vban::error vban::rpc_config::deserialize_toml (vban::tomlconfig & toml)
{
	if (!toml.empty ())
	{
		auto rpc_secure_l (toml.get_optional_child ("secure"));
		if (rpc_secure_l)
		{
			secure.deserialize_toml (*rpc_secure_l);
		}

		boost::asio::ip::address_v6 address_l;
		toml.get_optional<boost::asio::ip::address_v6> ("address", address_l, boost::asio::ip::address_v6::loopback ());
		address = address_l.to_string ();
		toml.get_optional<uint16_t> ("port", port);
		toml.get_optional<bool> ("enable_control", enable_control);
		toml.get_optional<uint8_t> ("max_json_depth", max_json_depth);
		toml.get_optional<uint64_t> ("max_request_size", max_request_size);

		auto rpc_logging_l (toml.get_optional_child ("logging"));
		if (rpc_logging_l)
		{
			rpc_logging_l->get_optional<bool> ("log_rpc", rpc_logging.log_rpc);
		}

		auto rpc_process_l (toml.get_optional_child ("process"));
		if (rpc_process_l)
		{
			rpc_process_l->get_optional<unsigned> ("io_threads", rpc_process.io_threads);
			rpc_process_l->get_optional<uint16_t> ("ipc_port", rpc_process.ipc_port);
			boost::asio::ip::address_v6 ipc_address_l;
			rpc_process_l->get_optional<boost::asio::ip::address_v6> ("ipc_address", ipc_address_l, boost::asio::ip::address_v6::loopback ());
			rpc_process.ipc_address = address_l.to_string ();
			rpc_process_l->get_optional<unsigned> ("num_ipc_connections", rpc_process.num_ipc_connections);
		}
	}

	return toml.get_error ();
}

vban::rpc_process_config::rpc_process_config () :
	ipc_address (boost::asio::ip::address_v6::loopback ().to_string ())
{
}

namespace vban
{
vban::error read_rpc_config_toml (boost::filesystem::path const & data_path_a, vban::rpc_config & config_a, std::vector<std::string> const & config_overrides)
{
	vban::error error;
	auto json_config_path = vban::get_rpc_config_path (data_path_a);
	auto toml_config_path = vban::get_rpc_toml_config_path (data_path_a);
	if (boost::filesystem::exists (json_config_path))
	{
		if (boost::filesystem::exists (toml_config_path))
		{
			error = "Both json and toml rpc configuration files exists. "
					"Either remove the config.json file and restart, or remove "
					"the config-rpc.toml file to start migration on next launch.";
		}
		else
		{
			// Migrate
			vban::rpc_config config_json_l;
			error = read_and_update_rpc_config (data_path_a, config_json_l);

			if (!error)
			{
				vban::tomlconfig toml_l;
				config_json_l.serialize_toml (toml_l);

				// Only write out non-default values
				vban::rpc_config config_defaults_l;
				vban::tomlconfig toml_defaults_l;
				config_defaults_l.serialize_toml (toml_defaults_l);

				toml_l.erase_default_values (toml_defaults_l);
				if (!toml_l.empty ())
				{
					toml_l.write (toml_config_path);
					boost::system::error_code error_chmod;
					vban::set_secure_perm_file (toml_config_path, error_chmod);
				}

				auto backup_path = data_path_a / "rpc_config_backup_toml_migration.json";
				boost::filesystem::rename (json_config_path, backup_path);
			}
		}
	}

	// Parse and deserialize
	vban::tomlconfig toml;

	std::stringstream config_overrides_stream;
	for (auto const & entry : config_overrides)
	{
		config_overrides_stream << entry << std::endl;
	}
	config_overrides_stream << std::endl;

	// Make sure we don't create an empty toml file if it doesn't exist. Running without a toml file is the default.
	if (!error)
	{
		if (boost::filesystem::exists (toml_config_path))
		{
			error = toml.read (config_overrides_stream, toml_config_path);
		}
		else
		{
			error = toml.read (config_overrides_stream);
		}
	}

	if (!error)
	{
		error = config_a.deserialize_toml (toml);
	}

	return error;
}

vban::error read_and_update_rpc_config (boost::filesystem::path const & data_path, vban::rpc_config & config_a)
{
	boost::system::error_code error_chmod;
	vban::jsonconfig json;
	auto config_path = vban::get_rpc_config_path (data_path);
	auto error (json.read_and_update (config_a, config_path));
	vban::set_secure_perm_file (config_path, error_chmod);
	return error;
}

std::string get_default_rpc_filepath ()
{
	boost::system::error_code err;
	auto running_executable_filepath = boost::dll::program_location (err);

	// Construct the vban_rpc executable file path based on where the currently running executable is found.
	auto rpc_filepath = running_executable_filepath.parent_path () / "vban_rpc";
	if (running_executable_filepath.has_extension ())
	{
		rpc_filepath.replace_extension (running_executable_filepath.extension ());
	}

	return rpc_filepath.string ();
}
}
