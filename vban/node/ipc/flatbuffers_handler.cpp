#include <vban/lib/errors.hpp>
#include <vban/node/ipc/action_handler.hpp>
#include <vban/node/ipc/flatbuffers_handler.hpp>
#include <vban/node/ipc/ipc_config.hpp>
#include <vban/node/ipc/ipc_server.hpp>

#include <boost/dll.hpp>
#include <boost/optional.hpp>

#include <iostream>
#include <sstream>
#include <unordered_map>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/minireflect.h>
#include <flatbuffers/registry.h>
#include <flatbuffers/util.h>

namespace
{
auto handler_map = vban::ipc::action_handler::handler_map ();

/**
 * A helper for when it's necessary to create a JSON error response manually
 */
std::string make_error_response (std::string const & error_message)
{
	std::ostringstream json;
	json << R"json({"message_type": "Error", "message": {"code": 1, "message": ")json"
		 << error_message
		 << R"json("}})json";
	return json.str ();
}

/**
 * Returns the 'api/flatbuffers' directory, boost::none if not found.
 * This searches the binary path as well as the parent (which is mostly useful for development)
 */
boost::optional<boost::filesystem::path> get_api_path ()
{
	auto parent_path = boost::dll::program_location ().parent_path ();
	if (!boost::filesystem::exists (parent_path / "api" / "flatbuffers"))
	{
		// See if the parent directory has the api subdirectories
		if (parent_path.has_parent_path ())
		{
			parent_path = boost::dll::program_location ().parent_path ().parent_path ();
		}

		if (!boost::filesystem::exists (parent_path / "api" / "flatbuffers"))
		{
			return boost::none;
		}
	}
	return parent_path / "api" / "flatbuffers";
}
}

vban::ipc::flatbuffers_handler::flatbuffers_handler (vban::node & node_a, vban::ipc::ipc_server & ipc_server_a, std::shared_ptr<vban::ipc::subscriber> const & subscriber_a, vban::ipc::ipc_config const & ipc_config_a) :
	node (node_a),
	ipc_server (ipc_server_a),
	subscriber (subscriber_a),
	ipc_config (ipc_config_a)
{
}

std::shared_ptr<flatbuffers::Parser> vban::ipc::flatbuffers_handler::make_flatbuffers_parser (vban::ipc::ipc_config const & ipc_config_a)
{
	auto parser (std::make_shared<flatbuffers::Parser> ());
	parser->opts.strict_json = true;
	parser->opts.skip_unexpected_fields_in_json = ipc_config_a.flatbuffers.skip_unexpected_fields_in_json;

	auto api_path = get_api_path ();
	if (!api_path)
	{
		throw vban::error ("Internal IPC error: unable to find api path");
	}

	const char * include_directories[] = { api_path->string ().c_str (), nullptr };
	std::string schemafile;
	if (!flatbuffers::LoadFile ((*api_path / "vbanapi.fbs").string ().c_str (), false, &schemafile))
	{
		throw vban::error ("Internal IPC error: unable to load schema file");
	}

	auto parse_success = parser->Parse (schemafile.c_str (), include_directories);
	if (!parse_success)
	{
		std::string parser_error = "Internal IPC error: unable to parse schema file: ";
		parser_error += parser->error_.c_str ();
		throw vban::error (parser_error);
	}
	return parser;
}

void vban::ipc::flatbuffers_handler::process_json (const uint8_t * message_buffer_a, size_t buffer_size_a,
std::function<void (std::shared_ptr<std::string> const &)> const & response_handler)
{
	try
	{
		if (!parser)
		{
			parser = make_flatbuffers_parser (ipc_config);
		}

		// Convert request from JSON
		auto body (std::string (reinterpret_cast<char *> (const_cast<uint8_t *> (message_buffer_a)), buffer_size_a));
		body += '\0';
		if (parser->Parse (reinterpret_cast<const char *> (body.data ())))
		{
			process (parser->builder_.GetBufferPointer (), parser->builder_.GetSize (), [parser = parser, response_handler] (std::shared_ptr<flatbuffers::FlatBufferBuilder> const & fbb) {
				// Convert response to JSON
				auto json (std::make_shared<std::string> ());
				if (!flatbuffers::GenerateText (*parser, fbb->GetBufferPointer (), json.get ()))
				{
					throw vban::error ("Couldn't serialize response to JSON");
				}

				response_handler (json);
			});
		}
		else
		{
			std::string parser_error = "Invalid message format: ";
			parser_error += parser->error_.c_str ();
			throw vban::error (parser_error);
		}
	}
	catch (vban::error const & err)
	{
		// Forces the parser construction to be retried as certain errors are
		// recoverable (such path errors getting fixed by the user without a node restart)
		parser = nullptr;

		// Convert error response to JSON. We must construct this manually since the exception
		// may be parser related (such as not being able to load the schema)
		response_handler (std::make_shared<std::string> (make_error_response (err.get_message ())));
	}
	catch (...)
	{
		std::cerr << "Unknown exception in " << __FUNCTION__ << std::endl;
		response_handler (std::make_shared<std::string> (make_error_response ("Unknown exception")));
	}
}

void vban::ipc::flatbuffers_handler::process (const uint8_t * message_buffer_a, size_t buffer_size_a,
std::function<void (std::shared_ptr<flatbuffers::FlatBufferBuilder> const &)> const & response_handler)
{
	auto buffer_l (std::make_shared<flatbuffers::FlatBufferBuilder> ());
	auto actionhandler (std::make_shared<action_handler> (node, ipc_server, subscriber, buffer_l));
	std::string correlationId = "";

	// Find and call the action handler
	try
	{
		// By default we verify the buffers, to make sure offsets reside inside the buffer.
		// This brings the buffer into cache, making the overall verify+parse overhead low.
		if (ipc_config.flatbuffers.verify_buffers)
		{
			auto verifier (flatbuffers::Verifier (message_buffer_a, buffer_size_a));
			if (!vbanapi::VerifyEnvelopeBuffer (verifier))
			{
				throw vban::error ("Envelope buffer did not pass verifier");
			}
		}

		auto incoming = vbanapi::GetEnvelope (message_buffer_a);
		if (incoming == nullptr)
		{
			vban::error err ("Invalid message");
			actionhandler->make_error (err.error_code_as_int (), err.get_message ());
			response_handler (buffer_l);
			return;
		}

		auto handler_method = handler_map.find (incoming->message_type ());
		if (handler_method != handler_map.end ())
		{
			if (incoming->correlation_id ())
			{
				actionhandler->set_correlation_id (incoming->correlation_id ()->str ());
			}
			handler_method->second (actionhandler.get (), *incoming);
		}
		else
		{
			vban::error err ("Unknown message type");
			actionhandler->make_error (err.error_code_as_int (), err.get_message ());
		}
	}
	catch (vban::error const & err)
	{
		actionhandler->make_error (err.error_code_as_int (), err.get_message ());
	}

	response_handler (buffer_l);
}
