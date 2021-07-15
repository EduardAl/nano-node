#pragma once

#include <vban/lib/config.hpp>
#include <vban/lib/errors.hpp>

namespace vban
{
class jsonconfig;
class tomlconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config ();
		vban::error deserialize_json (vban::jsonconfig & json_a);
		vban::error serialize_json (vban::jsonconfig & json) const;
		vban::error deserialize_toml (vban::tomlconfig & toml_a);
		vban::error serialize_toml (vban::tomlconfig & toml) const;
		vban::network_constants network_constants;
		bool enabled{ false };
		uint16_t port;
		std::string address;
	};
}
}
