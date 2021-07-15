#include <vban/node/confirmation_solicitor.hpp>
#include <vban/node/election.hpp>
#include <vban/node/network.hpp>
#include <vban/node/node.hpp>

#include <boost/format.hpp>

using namespace std::chrono;

std::chrono::milliseconds vban::election::base_latency () const
{
	return node.network_params.network.is_dev_network () ? 25ms : 1000ms;
}

vban::election_vote_result::election_vote_result (bool replay_a, bool processed_a)
{
	replay = replay_a;
	processed = processed_a;
}

vban::election::election (vban::node & node_a, std::shared_ptr<vban::block> const & block_a, std::function<void (std::shared_ptr<vban::block> const &)> const & confirmation_action_a, std::function<void (vban::account const &)> const & live_vote_action_a, vban::election_behavior election_behavior_a) :
	confirmation_action (confirmation_action_a),
	live_vote_action (live_vote_action_a),
	behavior (election_behavior_a),
	node (node_a),
	status ({ block_a, 0, 0, std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()), std::chrono::duration_values<std::chrono::milliseconds>::zero (), 0, 1, 0, vban::election_status_type::ongoing }),
	height (block_a->sideband ().height),
	root (block_a->root ()),
	qualified_root (block_a->qualified_root ())
{
	last_votes.emplace (node.network_params.random.not_an_account, vban::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () });
	last_blocks.emplace (block_a->hash (), block_a);
	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		node.active.generator.add (root, block_a->hash ());
	}
}

void vban::election::confirm_once (vban::unique_lock<vban::mutex> & lock_a, vban::election_status_type type_a)
{
	debug_assert (lock_a.owns_lock ());
	// This must be kept above the setting of election state, as dependent confirmed elections require up to date changes to election_winner_details
	vban::unique_lock<vban::mutex> election_winners_lk (node.active.election_winner_details_mutex);
	if (state_m.exchange (vban::election::state_t::confirmed) != vban::election::state_t::confirmed && (node.active.election_winner_details.count (status.winner->hash ()) == 0))
	{
		node.active.election_winner_details.emplace (status.winner->hash (), shared_from_this ());
		election_winners_lk.unlock ();
		status.election_end = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		status.confirmation_request_count = confirmation_request_count;
		status.block_count = vban::narrow_cast<decltype (status.block_count)> (last_blocks.size ());
		status.voter_count = vban::narrow_cast<decltype (status.voter_count)> (last_votes.size ());
		status.type = type_a;
		auto const status_l = status;
		lock_a.unlock ();
		node.process_confirmed (status_l);
		node.background ([node_l = node.shared (), status_l, confirmation_action_l = confirmation_action] () {
			if (confirmation_action_l)
			{
				confirmation_action_l (status_l.winner);
			}
		});
	}
	else
	{
		lock_a.unlock ();
	}
}

bool vban::election::valid_change (vban::election::state_t expected_a, vban::election::state_t desired_a) const
{
	bool result = false;
	switch (expected_a)
	{
		case vban::election::state_t::passive:
			switch (desired_a)
			{
				case vban::election::state_t::active:
				case vban::election::state_t::confirmed:
				case vban::election::state_t::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
			break;
		case vban::election::state_t::active:
			switch (desired_a)
			{
				case vban::election::state_t::confirmed:
				case vban::election::state_t::expired_unconfirmed:
					result = true;
					break;
				default:
					break;
			}
			break;
		case vban::election::state_t::confirmed:
			switch (desired_a)
			{
				case vban::election::state_t::expired_confirmed:
					result = true;
					break;
				default:
					break;
			}
			break;
		case vban::election::state_t::expired_unconfirmed:
		case vban::election::state_t::expired_confirmed:
			break;
	}
	return result;
}

bool vban::election::state_change (vban::election::state_t expected_a, vban::election::state_t desired_a)
{
	bool result = true;
	if (valid_change (expected_a, desired_a))
	{
		if (state_m.compare_exchange_strong (expected_a, desired_a))
		{
			state_start = std::chrono::steady_clock::now ().time_since_epoch ();
			result = false;
		}
	}
	return result;
}

void vban::election::send_confirm_req (vban::confirmation_solicitor & solicitor_a)
{
	if ((base_latency () * (optimistic () ? 10 : 5)) < (std::chrono::steady_clock::now () - last_req))
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		if (!solicitor_a.add (*this))
		{
			last_req = std::chrono::steady_clock::now ();
			++confirmation_request_count;
		}
	}
}

void vban::election::transition_active ()
{
	state_change (vban::election::state_t::passive, vban::election::state_t::active);
}

bool vban::election::confirmed () const
{
	return state_m == vban::election::state_t::confirmed || state_m == vban::election::state_t::expired_confirmed;
}

bool vban::election::failed () const
{
	return state_m == vban::election::state_t::expired_unconfirmed;
}

void vban::election::broadcast_block (vban::confirmation_solicitor & solicitor_a)
{
	if (base_latency () * 15 < std::chrono::steady_clock::now () - last_block)
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		if (!solicitor_a.broadcast (*this))
		{
			last_block = std::chrono::steady_clock::now ();
		}
	}
}

bool vban::election::transition_time (vban::confirmation_solicitor & solicitor_a)
{
	bool result = false;
	switch (state_m)
	{
		case vban::election::state_t::passive:
			if (base_latency () * passive_duration_factor < std::chrono::steady_clock::now ().time_since_epoch () - state_start.load ())
			{
				state_change (vban::election::state_t::passive, vban::election::state_t::active);
			}
			break;
		case vban::election::state_t::active:
			broadcast_block (solicitor_a);
			send_confirm_req (solicitor_a);
			break;
		case vban::election::state_t::confirmed:
			if (base_latency () * confirmed_duration_factor < std::chrono::steady_clock::now ().time_since_epoch () - state_start.load ())
			{
				result = true;
				state_change (vban::election::state_t::confirmed, vban::election::state_t::expired_confirmed);
			}
			break;
		case vban::election::state_t::expired_unconfirmed:
		case vban::election::state_t::expired_confirmed:
			debug_assert (false);
			break;
	}
	auto const optimistic_expiration_time = node.network_params.network.is_dev_network () ? 500 : 60 * 1000;
	auto const expire_time = std::chrono::milliseconds (optimistic () ? optimistic_expiration_time : 5 * 60 * 1000);
	if (!confirmed () && expire_time < std::chrono::steady_clock::now () - election_start)
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		// It is possible the election confirmed while acquiring the mutex
		// state_change returning true would indicate it
		if (!state_change (state_m.load (), vban::election::state_t::expired_unconfirmed))
		{
			result = true;
			if (node.config.logging.election_expiration_tally_logging ())
			{
				log_votes (tally_impl (), "Election expired: ");
			}
			status.type = vban::election_status_type::stopped;
		}
	}
	return result;
}

bool vban::election::have_quorum (vban::tally_t const & tally_a) const
{
	auto i (tally_a.begin ());
	++i;
	auto second (i != tally_a.end () ? i->first : 0);
	auto delta_l (node.online_reps.delta ());
	release_assert (tally_a.begin ()->first >= second);
	bool result{ (tally_a.begin ()->first - second) >= delta_l };
	return result;
}

vban::tally_t vban::election::tally () const
{
	vban::lock_guard<vban::mutex> guard (mutex);
	return tally_impl ();
}

vban::tally_t vban::election::tally_impl () const
{
	std::unordered_map<vban::block_hash, vban::uint256_t> block_weights;
	std::unordered_map<vban::block_hash, vban::uint256_t> final_weights_l;
	for (auto const & [account, info] : last_votes)
	{
		auto rep_weight (node.ledger.weight (account));
		block_weights[info.hash] += rep_weight;
		if (info.timestamp == std::numeric_limits<uint64_t>::max ())
		{
			final_weights_l[info.hash] += rep_weight;
		}
	}
	last_tally = block_weights;
	vban::tally_t result;
	for (auto const & [hash, amount] : block_weights)
	{
		auto block (last_blocks.find (hash));
		if (block != last_blocks.end ())
		{
			result.emplace (amount, block->second);
		}
	}
	// Calculate final votes sum for winner
	if (!final_weights_l.empty () && !result.empty ())
	{
		auto winner_hash (result.begin ()->second->hash ());
		auto find_final (final_weights_l.find (winner_hash));
		if (find_final != final_weights_l.end ())
		{
			final_weight = find_final->second;
		}
	}
	return result;
}

void vban::election::confirm_if_quorum (vban::unique_lock<vban::mutex> & lock_a)
{
	debug_assert (lock_a.owns_lock ());
	auto tally_l (tally_impl ());
	debug_assert (!tally_l.empty ());
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	auto const & winner_hash_l (block_l->hash ());
	status.tally = winner->first;
	status.final_tally = final_weight;
	auto const & status_winner_hash_l (status.winner->hash ());
	vban::uint256_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.online_reps.delta () && winner_hash_l != status_winner_hash_l)
	{
		status.winner = block_l;
		remove_votes (status_winner_hash_l);
		node.block_processor.force (block_l);
	}
	if (have_quorum (tally_l))
	{
		if (node.ledger.cache.final_votes_confirmation_canary.load () && !is_quorum.exchange (true) && node.config.enable_voting && node.wallets.reps ().voting > 0)
		{
			auto hash = status.winner->hash ();
			lock_a.unlock ();
			node.active.final_generator.add (root, hash);
			lock_a.lock ();
		}
		if (!node.ledger.cache.final_votes_confirmation_canary.load () || final_weight >= node.online_reps.delta ())
		{
			if (node.config.logging.vote_logging () || (node.config.logging.election_fork_tally_logging () && last_blocks.size () > 1))
			{
				log_votes (tally_l);
			}
			confirm_once (lock_a, vban::election_status_type::active_confirmed_quorum);
		}
	}
}

void vban::election::log_votes (vban::tally_t const & tally_a, std::string const & prefix_a) const
{
	std::stringstream tally;
	std::string line_end (node.config.logging.single_line_record () ? "\t" : "\n");
	tally << boost::str (boost::format ("%1%%2%Vote tally for root %3%, final weight:%4%") % prefix_a % line_end % root.to_string () % final_weight);
	for (auto i (tally_a.begin ()), n (tally_a.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("%1%Block %2% weight %3%") % line_end % i->second->hash ().to_string () % i->first.convert_to<std::string> ());
	}
	for (auto i (last_votes.begin ()), n (last_votes.end ()); i != n; ++i)
	{
		if (i->first != node.network_params.random.not_an_account)
		{
			tally << boost::str (boost::format ("%1%%2% %3% %4%") % line_end % i->first.to_account () % std::to_string (i->second.timestamp) % i->second.hash.to_string ());
		}
	}
	node.logger.try_log (tally.str ());
}

std::shared_ptr<vban::block> vban::election::find (vban::block_hash const & hash_a) const
{
	std::shared_ptr<vban::block> result;
	vban::lock_guard<vban::mutex> guard (mutex);
	if (auto existing = last_blocks.find (hash_a); existing != last_blocks.end ())
	{
		result = existing->second;
	}
	return result;
}

vban::election_vote_result vban::election::vote (vban::account const & rep, uint64_t timestamp_a, vban::block_hash const & block_hash_a)
{
	auto replay (false);
	auto online_stake (node.online_reps.trended ());
	auto weight (node.ledger.weight (rep));
	auto should_process (false);
	if (node.network_params.network.is_dev_network () || weight > node.minimum_principal_weight (online_stake))
	{
		unsigned int cooldown;
		if (weight < online_stake / 100) // 0.1% to 1%
		{
			cooldown = 15;
		}
		else if (weight < online_stake / 20) // 1% to 5%
		{
			cooldown = 5;
		}
		else // 5% or above
		{
			cooldown = 1;
		}

		vban::unique_lock<vban::mutex> lock (mutex);

		auto last_vote_it (last_votes.find (rep));
		if (last_vote_it == last_votes.end ())
		{
			should_process = true;
		}
		else
		{
			auto last_vote_l (last_vote_it->second);
			if (last_vote_l.timestamp < timestamp_a || (last_vote_l.timestamp == timestamp_a && last_vote_l.hash < block_hash_a))
			{
				auto max_vote = timestamp_a == std::numeric_limits<uint64_t>::max () && last_vote_l.timestamp < timestamp_a;
				auto past_cooldown = last_vote_l.time <= std::chrono::steady_clock::now () - std::chrono::seconds (cooldown);
				should_process = max_vote || past_cooldown;
			}
			else
			{
				replay = true;
			}
		}
		if (should_process)
		{
			node.stats.inc (vban::stat::type::election, vban::stat::detail::vote_new);
			last_votes[rep] = { std::chrono::steady_clock::now (), timestamp_a, block_hash_a };
			live_vote_action (rep);
			if (!confirmed ())
			{
				confirm_if_quorum (lock);
			}
		}
	}
	return vban::election_vote_result (replay, should_process);
}

bool vban::election::publish (std::shared_ptr<vban::block> const & block_a)
{
	vban::unique_lock<vban::mutex> lock (mutex);

	// Do not insert new blocks if already confirmed
	auto result (confirmed ());
	if (!result && last_blocks.size () >= max_blocks && last_blocks.find (block_a->hash ()) == last_blocks.end ())
	{
		if (!replace_by_weight (lock, block_a->hash ()))
		{
			result = true;
			node.network.publish_filter.clear (block_a);
		}
		debug_assert (lock.owns_lock ());
	}
	if (!result)
	{
		auto existing = last_blocks.find (block_a->hash ());
		if (existing == last_blocks.end ())
		{
			last_blocks.emplace (std::make_pair (block_a->hash (), block_a));
		}
		else
		{
			result = true;
			existing->second = block_a;
			if (status.winner->hash () == block_a->hash ())
			{
				status.winner = block_a;
				node.network.flood_block (block_a, vban::buffer_drop_policy::no_limiter_drop);
			}
		}
	}
	/*
	Result is true if:
	1) election is confirmed or expired
	2) given election contains 10 blocks & new block didn't receive enough votes to replace existing blocks
	3) given block in already in election & election contains less than 10 blocks (replacing block content with new)
	*/
	return result;
}

size_t vban::election::insert_inactive_votes_cache (vban::inactive_cache_information const & cache_a)
{
	vban::unique_lock<vban::mutex> lock (mutex);
	for (auto const & [rep, timestamp] : cache_a.voters)
	{
		auto inserted (last_votes.emplace (rep, vban::vote_info{ std::chrono::steady_clock::time_point::min (), timestamp, cache_a.hash }));
		if (inserted.second)
		{
			node.stats.inc (vban::stat::type::election, vban::stat::detail::vote_cached);
		}
	}
	if (!confirmed ())
	{
		if (!cache_a.voters.empty ())
		{
			auto delay (std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now () - cache_a.arrival));
			if (delay > late_blocks_delay)
			{
				node.stats.inc (vban::stat::type::election, vban::stat::detail::late_block);
				node.stats.add (vban::stat::type::election, vban::stat::detail::late_block_seconds, vban::stat::dir::in, delay.count (), true);
			}
		}
		if (last_votes.size () > 1) // not_an_account
		{
			// Even if no votes were in cache, they could be in the election
			confirm_if_quorum (lock);
		}
	}
	return cache_a.voters.size ();
}

bool vban::election::optimistic () const
{
	return behavior == vban::election_behavior::optimistic;
}

vban::election_extended_status vban::election::current_status () const
{
	vban::lock_guard<vban::mutex> guard (mutex);
	vban::election_status status_l = status;
	status_l.confirmation_request_count = confirmation_request_count;
	status_l.block_count = vban::narrow_cast<decltype (status_l.block_count)> (last_blocks.size ());
	status_l.voter_count = vban::narrow_cast<decltype (status_l.voter_count)> (last_votes.size ());
	return vban::election_extended_status{ status_l, last_votes, tally_impl () };
}

std::shared_ptr<vban::block> vban::election::winner () const
{
	vban::lock_guard<vban::mutex> guard (mutex);
	return status.winner;
}

void vban::election::generate_votes () const
{
	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		vban::unique_lock<vban::mutex> lock (mutex);
		if (confirmed () || have_quorum (tally_impl ()))
		{
			auto hash = status.winner->hash ();
			lock.unlock ();
			node.active.final_generator.add (root, hash);
			lock.lock ();
		}
		else
		{
			node.active.generator.add (root, status.winner->hash ());
		}
	}
}

void vban::election::remove_votes (vban::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	if (node.config.enable_voting && node.wallets.reps ().voting > 0)
	{
		// Remove votes from election
		auto list_generated_votes (node.history.votes (root, hash_a));
		for (auto const & vote : list_generated_votes)
		{
			last_votes.erase (vote->account);
		}
		// Clear votes cache
		node.history.erase (root);
	}
}

void vban::election::remove_block (vban::block_hash const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	if (status.winner->hash () != hash_a)
	{
		if (auto existing = last_blocks.find (hash_a); existing != last_blocks.end ())
		{
			for (auto i (last_votes.begin ()); i != last_votes.end ();)
			{
				if (i->second.hash == hash_a)
				{
					i = last_votes.erase (i);
				}
				else
				{
					++i;
				}
			}
			node.network.publish_filter.clear (existing->second);
			last_blocks.erase (hash_a);
		}
	}
}

bool vban::election::replace_by_weight (vban::unique_lock<vban::mutex> & lock_a, vban::block_hash const & hash_a)
{
	debug_assert (lock_a.owns_lock ());
	vban::block_hash replaced_block (0);
	auto winner_hash (status.winner->hash ());
	// Sort existing blocks tally
	std::vector<std::pair<vban::block_hash, vban::uint256_t>> sorted;
	sorted.reserve (last_tally.size ());
	std::copy (last_tally.begin (), last_tally.end (), std::back_inserter (sorted));
	lock_a.unlock ();
	// Sort in ascending order
	std::sort (sorted.begin (), sorted.end (), [] (auto const & left, auto const & right) { return left.second < right.second; });
	// Replace if lowest tally is below inactive cache new block weight
	auto inactive_existing (node.active.find_inactive_votes_cache (hash_a));
	auto inactive_tally (inactive_existing.status.tally);
	if (inactive_tally > 0 && sorted.size () < max_blocks)
	{
		// If count of tally items is less than 10, remove any block without tally
		for (auto const & [hash, block] : blocks ())
		{
			if (std::find_if (sorted.begin (), sorted.end (), [&hash = hash] (auto const & item_a) { return item_a.first == hash; }) == sorted.end () && hash != winner_hash)
			{
				replaced_block = hash;
				break;
			}
		}
	}
	else if (inactive_tally > 0 && inactive_tally > sorted.front ().second)
	{
		if (sorted.front ().first != winner_hash)
		{
			replaced_block = sorted.front ().first;
		}
		else if (inactive_tally > sorted[1].second)
		{
			// Avoid removing winner
			replaced_block = sorted[1].first;
		}
	}

	bool replaced (false);
	if (!replaced_block.is_zero ())
	{
		node.active.erase_hash (replaced_block);
		lock_a.lock ();
		remove_block (replaced_block);
		replaced = true;
	}
	else
	{
		lock_a.lock ();
	}
	return replaced;
}

void vban::election::force_confirm (vban::election_status_type type_a)
{
	release_assert (node.network_params.network.is_dev_network ());
	vban::unique_lock<vban::mutex> lock (mutex);
	confirm_once (lock, type_a);
}

std::unordered_map<vban::block_hash, std::shared_ptr<vban::block>> vban::election::blocks () const
{
	vban::lock_guard<vban::mutex> guard (mutex);
	return last_blocks;
}

std::unordered_map<vban::account, vban::vote_info> vban::election::votes () const
{
	vban::lock_guard<vban::mutex> guard (mutex);
	return last_votes;
}

std::vector<vban::vote_with_weight_info> vban::election::votes_with_weight () const
{
	std::multimap<vban::uint256_t, vban::vote_with_weight_info, std::greater<vban::uint256_t>> sorted_votes;
	std::vector<vban::vote_with_weight_info> result;
	auto votes_l (votes ());
	for (auto const & vote_l : votes_l)
	{
		if (vote_l.first != node.network_params.random.not_an_account)
		{
			auto amount (node.ledger.cache.rep_weights.representation_get (vote_l.first));
			vban::vote_with_weight_info vote_info{ vote_l.first, vote_l.second.time, vote_l.second.timestamp, vote_l.second.hash, amount };
			sorted_votes.emplace (std::move (amount), vote_info);
		}
	}
	result.reserve (sorted_votes.size ());
	std::transform (sorted_votes.begin (), sorted_votes.end (), std::back_inserter (result), [] (auto const & entry) { return entry.second; });
	return result;
}
