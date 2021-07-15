#pragma once

#include <vban/lib/errors.hpp>
#include <vban/lib/numbers.hpp>

#include <string>

namespace vban
{
class tomlconfig;

/** Configuration options for the Qt wallet */
class wallet_config final
{
public:
	wallet_config ();
	/** Update this instance by parsing the given wallet and account */
	vban::error parse (std::string const & wallet_a, std::string const & account_a);
	vban::error serialize_toml (vban::tomlconfig & toml_a) const;
	vban::error deserialize_toml (vban::tomlconfig & toml_a);
	vban::wallet_id wallet;
	vban::account account{ 0 };
};
}
