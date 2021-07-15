#pragma once

#include <vban/lib/numbers.hpp>

#include <type_traits>
#include <unordered_map>

namespace vban
{
/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_begin = 2,
	epoch_0 = 2,
	epoch_1 = 3,
	epoch_2 = 4,
	max = epoch_2
};

/* This turns epoch_0 into 0 for instance */
std::underlying_type_t<vban::epoch> normalized_epoch (vban::epoch epoch_a);
}
namespace std
{
template <>
struct hash<::vban::epoch>
{
	std::size_t operator() (::vban::epoch const & epoch_a) const
	{
		std::hash<std::underlying_type_t<::vban::epoch>> hash;
		return hash (static_cast<std::underlying_type_t<::vban::epoch>> (epoch_a));
	}
};
}
namespace vban
{
class epoch_info
{
public:
	vban::public_key signer;
	vban::link link;
};
class epochs
{
public:
	bool is_epoch_link (vban::link const & link_a) const;
	vban::link const & link (vban::epoch epoch_a) const;
	vban::public_key const & signer (vban::epoch epoch_a) const;
	vban::epoch epoch (vban::link const & link_a) const;
	void add (vban::epoch epoch_a, vban::public_key const & signer_a, vban::link const & link_a);
	/** Checks that new_epoch is 1 version higher than epoch */
	static bool is_sequential (vban::epoch epoch_a, vban::epoch new_epoch_a);

private:
	std::unordered_map<vban::epoch, vban::epoch_info> epochs_m;
};
}
