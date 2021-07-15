#pragma once

#include <vban/lib/errors.hpp>

#include <chrono>

namespace vban
{
class jsonconfig;
class tomlconfig;
class txn_tracking_config final
{
public:
	/** If true, enable tracking for transaction read/writes held open longer than the min time variables */
	bool enable{ false };
	std::chrono::milliseconds min_read_txn_time{ 5000 };
	std::chrono::milliseconds min_write_txn_time{ 500 };
	bool ignore_writes_below_block_processor_max_time{ true };
};

/** Configuration options for diagnostics information */
class diagnostics_config final
{
public:
	vban::error serialize_json (vban::jsonconfig &) const;
	vban::error deserialize_json (vban::jsonconfig &);
	vban::error serialize_toml (vban::tomlconfig &) const;
	vban::error deserialize_toml (vban::tomlconfig &);

	txn_tracking_config txn_tracking;
};
}
