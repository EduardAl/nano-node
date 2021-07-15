#include <vban/node/nodeconfig.hpp>
#include <vban/node/online_reps.hpp>
#include <vban/secure/blockstore.hpp>
#include <vban/secure/ledger.hpp>

vban::online_reps::online_reps (vban::ledger & ledger_a, vban::node_config const & config_a) :
	ledger{ ledger_a },
	config{ config_a }
{
	if (!ledger.store.init_error ())
	{
		auto transaction (ledger.store.tx_begin_read ());
		trended_m = calculate_trend (transaction);
	}
}

void vban::online_reps::observe (vban::account const & rep_a)
{
	if (ledger.weight (rep_a) > 0)
	{
		vban::lock_guard<vban::mutex> lock (mutex);
		auto now = std::chrono::steady_clock::now ();
		auto new_insert = reps.get<tag_account> ().erase (rep_a) == 0;
		reps.insert ({ now, rep_a });
		auto cutoff = reps.get<tag_time> ().lower_bound (now - std::chrono::seconds (config.network_params.node.weight_period));
		auto trimmed = reps.get<tag_time> ().begin () != cutoff;
		reps.get<tag_time> ().erase (reps.get<tag_time> ().begin (), cutoff);
		if (new_insert || trimmed)
		{
			online_m = calculate_online ();
		}
	}
}

void vban::online_reps::sample ()
{
	vban::unique_lock<vban::mutex> lock (mutex);
	vban::uint256_t online_l = online_m;
	lock.unlock ();
	vban::uint256_t trend_l;
	{
		auto transaction (ledger.store.tx_begin_write ({ tables::online_weight }));
		// Discard oldest entries
		while (ledger.store.online_weight_count (transaction) >= config.network_params.node.max_weight_samples)
		{
			auto oldest (ledger.store.online_weight_begin (transaction));
			debug_assert (oldest != ledger.store.online_weight_end ());
			ledger.store.online_weight_del (transaction, oldest->first);
		}
		ledger.store.online_weight_put (transaction, std::chrono::system_clock::now ().time_since_epoch ().count (), online_l);
		trend_l = calculate_trend (transaction);
	}
	lock.lock ();
	trended_m = trend_l;
}

vban::uint256_t vban::online_reps::calculate_online () const
{
	vban::uint256_t current;
	for (auto & i : reps)
	{
		current += ledger.weight (i.account);
	}
	return current;
}

vban::uint256_t vban::online_reps::calculate_trend (vban::transaction & transaction_a) const
{
	std::vector<vban::uint256_t> items;
	items.reserve (config.network_params.node.max_weight_samples + 1);
	items.push_back (config.online_weight_minimum.number ());
	for (auto i (ledger.store.online_weight_begin (transaction_a)), n (ledger.store.online_weight_end ()); i != n; ++i)
	{
		items.push_back (i->second.number ());
	}
	vban::uint256_t result;
	// Pick median value for our target vote weight
	auto median_idx = items.size () / 2;
	nth_element (items.begin (), items.begin () + median_idx, items.end ());
	result = items[median_idx];
	return result;
}

vban::uint256_t vban::online_reps::trended () const
{
	vban::lock_guard<vban::mutex> lock (mutex);
	return trended_m;
}

vban::uint256_t vban::online_reps::online () const
{
	vban::lock_guard<vban::mutex> lock (mutex);
	return online_m;
}

vban::uint256_t vban::online_reps::delta () const
{
	vban::lock_guard<vban::mutex> lock (mutex);
	// Using a larger container to ensure maximum precision
	auto weight = static_cast<vban::uint256_t> (std::max ({ online_m, trended_m, config.online_weight_minimum.number () }));
	return ((weight * online_weight_quorum) / 100).convert_to<vban::uint256_t> ();
}

std::vector<vban::account> vban::online_reps::list ()
{
	std::vector<vban::account> result;
	vban::lock_guard<vban::mutex> lock (mutex);
	std::for_each (reps.begin (), reps.end (), [&result] (rep_info const & info_a) { result.push_back (info_a.account); });
	return result;
}

void vban::online_reps::clear ()
{
	vban::lock_guard<vban::mutex> lock (mutex);
	reps.clear ();
	online_m = 0;
}

std::unique_ptr<vban::container_info_component> vban::collect_container_info (online_reps & online_reps, std::string const & name)
{
	size_t count;
	{
		vban::lock_guard<vban::mutex> guard (online_reps.mutex);
		count = online_reps.reps.size ();
	}

	auto sizeof_element = sizeof (decltype (online_reps.reps)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "reps", count, sizeof_element }));
	return composite;
}
