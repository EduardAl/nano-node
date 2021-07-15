#pragma once

#include <vban/lib/errors.hpp>

namespace vban
{
class jsonconfig;
class tomlconfig;
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	vban::error serialize_json (vban::jsonconfig &) const;
	vban::error deserialize_json (vban::jsonconfig &);
	vban::error serialize_toml (vban::tomlconfig &) const;
	vban::error deserialize_toml (vban::tomlconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
