#include <vban/node/lmdb/wallet_value.hpp>

vban::wallet_value::wallet_value (vban::db_val<MDB_val> const & val_a)
{
	debug_assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

vban::wallet_value::wallet_value (vban::raw_key const & key_a, uint64_t work_a) :
	key (key_a),
	work (work_a)
{
}

vban::db_val<MDB_val> vban::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return vban::db_val<MDB_val> (sizeof (*this), const_cast<vban::wallet_value *> (this));
}
