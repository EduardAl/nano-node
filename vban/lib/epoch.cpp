#include <vban/lib/epoch.hpp>
#include <vban/lib/utility.hpp>

vban::link const & vban::epochs::link (vban::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).link;
}

bool vban::epochs::is_epoch_link (vban::link const & link_a) const
{
	return std::any_of (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; });
}

vban::public_key const & vban::epochs::signer (vban::epoch epoch_a) const
{
	return epochs_m.at (epoch_a).signer;
}

vban::epoch vban::epochs::epoch (vban::link const & link_a) const
{
	auto existing (std::find_if (epochs_m.begin (), epochs_m.end (), [&link_a] (auto const & item_a) { return item_a.second.link == link_a; }));
	debug_assert (existing != epochs_m.end ());
	return existing->first;
}

void vban::epochs::add (vban::epoch epoch_a, vban::public_key const & signer_a, vban::link const & link_a)
{
	debug_assert (epochs_m.find (epoch_a) == epochs_m.end ());
	epochs_m[epoch_a] = { signer_a, link_a };
}

bool vban::epochs::is_sequential (vban::epoch epoch_a, vban::epoch new_epoch_a)
{
	auto head_epoch = std::underlying_type_t<vban::epoch> (epoch_a);
	bool is_valid_epoch (head_epoch >= std::underlying_type_t<vban::epoch> (vban::epoch::epoch_0));
	return is_valid_epoch && (std::underlying_type_t<vban::epoch> (new_epoch_a) == (head_epoch + 1));
}

std::underlying_type_t<vban::epoch> vban::normalized_epoch (vban::epoch epoch_a)
{
	// Currently assumes that the epoch versions in the enum are sequential.
	auto start = std::underlying_type_t<vban::epoch> (vban::epoch::epoch_0);
	auto end = std::underlying_type_t<vban::epoch> (epoch_a);
	debug_assert (end >= start);
	return end - start;
}
