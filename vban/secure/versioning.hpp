#pragma once

#include <vban/lib/blocks.hpp>
#include <vban/secure/common.hpp>

struct MDB_val;

namespace vban
{
class pending_info_v14 final
{
public:
	pending_info_v14 () = default;
	pending_info_v14 (vban::account const &, vban::amount const &, vban::epoch);
	size_t db_size () const;
	bool deserialize (vban::stream &);
	bool operator== (vban::pending_info_v14 const &) const;
	vban::account source{ 0 };
	vban::amount amount{ 0 };
	vban::epoch epoch{ vban::epoch::epoch_0 };
};
class account_info_v14 final
{
public:
	account_info_v14 () = default;
	account_info_v14 (vban::block_hash const &, vban::block_hash const &, vban::block_hash const &, vban::amount const &, uint64_t, uint64_t, uint64_t, vban::epoch);
	size_t db_size () const;
	vban::block_hash head{ 0 };
	vban::block_hash rep_block{ 0 };
	vban::block_hash open_block{ 0 };
	vban::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	uint64_t confirmation_height{ 0 };
	vban::epoch epoch{ vban::epoch::epoch_0 };
};
class block_sideband_v14 final
{
public:
	block_sideband_v14 () = default;
	block_sideband_v14 (vban::block_type, vban::account const &, vban::block_hash const &, vban::amount const &, uint64_t, uint64_t);
	void serialize (vban::stream &) const;
	bool deserialize (vban::stream &);
	static size_t size (vban::block_type);
	vban::block_type type{ vban::block_type::invalid };
	vban::block_hash successor{ 0 };
	vban::account account{ 0 };
	vban::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
};
class state_block_w_sideband_v14
{
public:
	std::shared_ptr<vban::state_block> state_block;
	vban::block_sideband_v14 sideband;
};
class block_sideband_v18 final
{
public:
	block_sideband_v18 () = default;
	block_sideband_v18 (vban::account const &, vban::block_hash const &, vban::amount const &, uint64_t, uint64_t, vban::block_details const &);
	block_sideband_v18 (vban::account const &, vban::block_hash const &, vban::amount const &, uint64_t, uint64_t, vban::epoch, bool is_send, bool is_receive, bool is_epoch);
	void serialize (vban::stream &, vban::block_type) const;
	bool deserialize (vban::stream &, vban::block_type);
	static size_t size (vban::block_type);
	vban::block_hash successor{ 0 };
	vban::account account{ 0 };
	vban::amount balance{ 0 };
	uint64_t height{ 0 };
	uint64_t timestamp{ 0 };
	vban::block_details details;
};
}
