#include <vban/lib/rep_weights.hpp>
#include <vban/secure/blockstore.hpp>

void vban::rep_weights::representation_add (vban::account const & source_rep_a, vban::uint256_t const & amount_a)
{
	vban::lock_guard<vban::mutex> guard (mutex);
	auto source_previous (get (source_rep_a));
	put (source_rep_a, source_previous + amount_a);
}

void vban::rep_weights::representation_add_dual (vban::account const & source_rep_1, vban::uint256_t const & amount_1, vban::account const & source_rep_2, vban::uint256_t const & amount_2)
{
	if (source_rep_1 != source_rep_2)
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		auto source_previous_1 (get (source_rep_1));
		put (source_rep_1, source_previous_1 + amount_1);
		auto source_previous_2 (get (source_rep_2));
		put (source_rep_2, source_previous_2 + amount_2);
	}
	else
	{
		representation_add (source_rep_1, amount_1 + amount_2);
	}
}

void vban::rep_weights::representation_put (vban::account const & account_a, vban::uint128_union const & representation_a)
{
	vban::lock_guard<vban::mutex> guard (mutex);
	put (account_a, representation_a);
}

vban::uint256_t vban::rep_weights::representation_get (vban::account const & account_a) const
{
	vban::lock_guard<vban::mutex> lk (mutex);
	return get (account_a);
}

/** Makes a copy */
std::unordered_map<vban::account, vban::uint256_t> vban::rep_weights::get_rep_amounts () const
{
	vban::lock_guard<vban::mutex> guard (mutex);
	return rep_amounts;
}

void vban::rep_weights::copy_from (vban::rep_weights & other_a)
{
	vban::lock_guard<vban::mutex> guard_this (mutex);
	vban::lock_guard<vban::mutex> guard_other (other_a.mutex);
	for (auto const & entry : other_a.rep_amounts)
	{
		auto prev_amount (get (entry.first));
		put (entry.first, prev_amount + entry.second);
	}
}

void vban::rep_weights::put (vban::account const & account_a, vban::uint128_union const & representation_a)
{
	auto it = rep_amounts.find (account_a);
	auto amount = representation_a.number ();
	if (it != rep_amounts.end ())
	{
		it->second = amount;
	}
	else
	{
		rep_amounts.emplace (account_a, amount);
	}
}

vban::uint256_t vban::rep_weights::get (vban::account const & account_a) const
{
	auto it = rep_amounts.find (account_a);
	if (it != rep_amounts.end ())
	{
		return it->second;
	}
	else
	{
		return vban::uint256_t{ 0 };
	}
}

std::unique_ptr<vban::container_info_component> vban::collect_container_info (vban::rep_weights const & rep_weights, std::string const & name)
{
	size_t rep_amounts_count;

	{
		vban::lock_guard<vban::mutex> guard (rep_weights.mutex);
		rep_amounts_count = rep_weights.rep_amounts.size ();
	}
	auto sizeof_element = sizeof (decltype (rep_weights.rep_amounts)::value_type);
	auto composite = std::make_unique<vban::container_info_composite> (name);
	composite->add_component (std::make_unique<vban::container_info_leaf> (container_info{ "rep_amounts", rep_amounts_count, sizeof_element }));
	return composite;
}
