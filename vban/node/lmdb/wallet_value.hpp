#pragma once

#include <vban/lib/numbers.hpp>
#include <vban/secure/blockstore.hpp>

#include <lmdb/libraries/liblmdb/lmdb.h>

namespace vban
{
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (vban::db_val<MDB_val> const &);
	wallet_value (vban::raw_key const &, uint64_t);
	vban::db_val<MDB_val> val () const;
	vban::raw_key key;
	uint64_t work;
};
}
