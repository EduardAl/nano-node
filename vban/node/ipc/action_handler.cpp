#include <vban/ipc_flatbuffers_lib/generated/flatbuffers/vbanapi_generated.h>
#include <vban/lib/errors.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/node/ipc/action_handler.hpp>
#include <vban/node/ipc/ipc_server.hpp>
#include <vban/node/node.hpp>

#include <iostream>

namespace
{
vban::account parse_account (std::string const & account, bool & out_is_deprecated_format)
{
	vban::account result (0);
	if (account.empty ())
	{
		throw vban::error (vban::error_common::bad_account_number);
	}
	if (result.decode_account (account))
	{
		throw vban::error (vban::error_common::bad_account_number);
	}
	else if (account[3] == '-' || account[4] == '-')
	{
		out_is_deprecated_format = true;
	}

	return result;
}
/** Returns the message as a Flatbuffers ObjectAPI type, managed by a unique_ptr */
template <typename T>
auto get_message (vbanapi::Envelope const & envelope)
{
	auto raw (envelope.message_as<T> ()->UnPack ());
	return std::unique_ptr<typename T::NativeTableType> (raw);
}
}

/**
 * Mapping from message type to handler function.
 * @note This must be updated whenever a new message type is added to the Flatbuffers IDL.
 */
auto vban::ipc::action_handler::handler_map () -> std::unordered_map<vbanapi::Message, std::function<void (vban::ipc::action_handler *, vbanapi::Envelope const &)>, vban::ipc::enum_hash>
{
	static std::unordered_map<vbanapi::Message, std::function<void (vban::ipc::action_handler *, vbanapi::Envelope const &)>, vban::ipc::enum_hash> handlers;
	if (handlers.empty ())
	{
		handlers.emplace (vbanapi::Message::Message_IsAlive, &vban::ipc::action_handler::on_is_alive);
		handlers.emplace (vbanapi::Message::Message_TopicConfirmation, &vban::ipc::action_handler::on_topic_confirmation);
		handlers.emplace (vbanapi::Message::Message_AccountWeight, &vban::ipc::action_handler::on_account_weight);
		handlers.emplace (vbanapi::Message::Message_ServiceRegister, &vban::ipc::action_handler::on_service_register);
		handlers.emplace (vbanapi::Message::Message_ServiceStop, &vban::ipc::action_handler::on_service_stop);
		handlers.emplace (vbanapi::Message::Message_TopicServiceStop, &vban::ipc::action_handler::on_topic_service_stop);
	}
	return handlers;
}

vban::ipc::action_handler::action_handler (vban::node & node_a, vban::ipc::ipc_server & server_a, std::weak_ptr<vban::ipc::subscriber> const & subscriber_a, std::shared_ptr<flatbuffers::FlatBufferBuilder> const & builder_a) :
	flatbuffer_producer (builder_a),
	node (node_a),
	ipc_server (server_a),
	subscriber (subscriber_a)
{
}

void vban::ipc::action_handler::on_topic_confirmation (vbanapi::Envelope const & envelope_a)
{
	auto confirmationTopic (get_message<vbanapi::TopicConfirmation> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (confirmationTopic));
	vbanapi::EventAckT ack;
	create_response (ack);
}

void vban::ipc::action_handler::on_service_register (vbanapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { vban::ipc::access_permission::api_service_register, vban::ipc::access_permission::service });
	auto query (get_message<vbanapi::ServiceRegister> (envelope_a));
	ipc_server.get_broker ()->service_register (query->service_name, this->subscriber);
	vbanapi::SuccessT success;
	create_response (success);
}

void vban::ipc::action_handler::on_service_stop (vbanapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { vban::ipc::access_permission::api_service_stop, vban::ipc::access_permission::service });
	auto query (get_message<vbanapi::ServiceStop> (envelope_a));
	if (query->service_name == "node")
	{
		ipc_server.node.stop ();
	}
	else
	{
		ipc_server.get_broker ()->service_stop (query->service_name);
	}
	vbanapi::SuccessT success;
	create_response (success);
}

void vban::ipc::action_handler::on_topic_service_stop (vbanapi::Envelope const & envelope_a)
{
	auto topic (get_message<vbanapi::TopicServiceStop> (envelope_a));
	ipc_server.get_broker ()->subscribe (subscriber, std::move (topic));
	vbanapi::EventAckT ack;
	create_response (ack);
}

void vban::ipc::action_handler::on_account_weight (vbanapi::Envelope const & envelope_a)
{
	require_oneof (envelope_a, { vban::ipc::access_permission::api_account_weight, vban::ipc::access_permission::account_query });
	bool is_deprecated_format{ false };
	auto query (get_message<vbanapi::AccountWeight> (envelope_a));
	auto balance (node.weight (parse_account (query->account, is_deprecated_format)));

	vbanapi::AccountWeightResponseT response;
	response.voting_weight = balance.str ();
	create_response (response);
}

void vban::ipc::action_handler::on_is_alive (vbanapi::Envelope const & envelope)
{
	vbanapi::IsAliveT alive;
	create_response (alive);
}

bool vban::ipc::action_handler::has_access (vbanapi::Envelope const & envelope_a, vban::ipc::access_permission permission_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access (credentials, permission_a);
}

bool vban::ipc::action_handler::has_access_to_all (vbanapi::Envelope const & envelope_a, std::initializer_list<vban::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_all (credentials, permissions_a);
}

bool vban::ipc::action_handler::has_access_to_oneof (vbanapi::Envelope const & envelope_a, std::initializer_list<vban::ipc::access_permission> permissions_a) const noexcept
{
	// If credentials are missing in the envelope, the default user is used
	std::string credentials;
	if (envelope_a.credentials () != nullptr)
	{
		credentials = envelope_a.credentials ()->str ();
	}

	return ipc_server.get_access ().has_access_to_oneof (credentials, permissions_a);
}

void vban::ipc::action_handler::require (vbanapi::Envelope const & envelope_a, vban::ipc::access_permission permission_a) const
{
	if (!has_access (envelope_a, permission_a))
	{
		throw vban::error (vban::error_common::access_denied);
	}
}

void vban::ipc::action_handler::require_all (vbanapi::Envelope const & envelope_a, std::initializer_list<vban::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_all (envelope_a, permissions_a))
	{
		throw vban::error (vban::error_common::access_denied);
	}
}

void vban::ipc::action_handler::require_oneof (vbanapi::Envelope const & envelope_a, std::initializer_list<vban::ipc::access_permission> permissions_a) const
{
	if (!has_access_to_oneof (envelope_a, permissions_a))
	{
		throw vban::error (vban::error_common::access_denied);
	}
}
