#include <vban/lib/jsonconfig.hpp>
#include <vban/lib/tomlconfig.hpp>
#include <vban/node/openclconfig.hpp>

vban::opencl_config::opencl_config (unsigned platform_a, unsigned device_a, unsigned threads_a) :
	platform (platform_a),
	device (device_a),
	threads (threads_a)
{
}

vban::error vban::opencl_config::serialize_json (vban::jsonconfig & json) const
{
	json.put ("platform", platform);
	json.put ("device", device);
	json.put ("threads", threads);
	return json.get_error ();
}

vban::error vban::opencl_config::deserialize_json (vban::jsonconfig & json)
{
	json.get_optional<unsigned> ("platform", platform);
	json.get_optional<unsigned> ("device", device);
	json.get_optional<unsigned> ("threads", threads);
	return json.get_error ();
}

vban::error vban::opencl_config::serialize_toml (vban::tomlconfig & toml) const
{
	toml.put ("platform", platform);
	toml.put ("device", device);
	toml.put ("threads", threads);

	// Add documentation
	toml.doc ("platform", "OpenCL platform identifier");
	toml.doc ("device", "OpenCL device identifier");
	toml.doc ("threads", "OpenCL thread count");

	return toml.get_error ();
}

vban::error vban::opencl_config::deserialize_toml (vban::tomlconfig & toml)
{
	toml.get_optional<unsigned> ("platform", platform);
	toml.get_optional<unsigned> ("device", device);
	toml.get_optional<unsigned> ("threads", threads);
	return toml.get_error ();
}
