#pragma once

#include <vban/lib/errors.hpp>

#include <thread>

namespace vban
{
class tomlconfig;

/** Configuration options for RocksDB */
class rocksdb_config final
{
public:
	vban::error serialize_toml (vban::tomlconfig & toml_a) const;
	vban::error deserialize_toml (vban::tomlconfig & toml_a);

	bool enable{ false };
	uint8_t memory_multiplier{ 2 };
	unsigned io_threads{ std::thread::hardware_concurrency () };
};
}
