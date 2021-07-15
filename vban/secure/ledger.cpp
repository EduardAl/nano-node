#include <vban/lib/rep_weights.hpp>
#include <vban/lib/stats.hpp>
#include <vban/lib/utility.hpp>
#include <vban/lib/work.hpp>
#include <vban/secure/blockstore.hpp>
#include <vban/secure/common.hpp>
#include <vban/secure/ledger.hpp>

#include <crypto/cryptopp/words.h>

namespace
{
/**
 * Roll back the visited block
 */
class rollback_visitor : public vban::block_visitor
{
public:
	rollback_visitor (vban::write_transaction const & transaction_a, vban::ledger & ledger_a, std::vector<std::shared_ptr<vban::block>> & list_a) :
		transaction (transaction_a),
		ledger (ledger_a),
		list (list_a)
	{
	}
	virtual ~rollback_visitor () = default;
	void send_block (vban::send_block const & block_a) override
	{
		auto hash (block_a.hash ());
		vban::pending_info pending;
		vban::pending_key key (block_a.hashables.destination, hash);
		while (!error && ledger.store.pending_get (transaction, key, pending))
		{
			error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.destination), list);
		}
		if (!error)
		{
			vban::account_info info;
			[[maybe_unused]] auto error (ledger.store.account_get (transaction, pending.source, info));
			debug_assert (!error);
			ledger.store.pending_del (transaction, key);
			ledger.cache.rep_weights.representation_add (info.representative, pending.amount.number ());
			vban::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), vban::seconds_since_epoch (), info.block_count - 1, vban::epoch::epoch_0);
			ledger.update_account (transaction, pending.source, info, new_info);
			ledger.store.block_del (transaction, hash);
			ledger.store.frontier_del (transaction, hash);
			ledger.store.frontier_put (transaction, block_a.hashables.previous, pending.source);
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			ledger.stats.inc (vban::stat::type::rollback, vban::stat::detail::send);
		}
	}
	void receive_block (vban::receive_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, hash));
		auto destination_account (ledger.account (transaction, hash));
		// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
		[[maybe_unused]] bool is_pruned (false);
		auto source_account (ledger.account_safe (transaction, block_a.hashables.source, is_pruned));
		vban::account_info info;
		[[maybe_unused]] auto error (ledger.store.account_get (transaction, destination_account, info));
		debug_assert (!error);
		ledger.cache.rep_weights.representation_add (info.representative, 0 - amount);
		vban::account_info new_info (block_a.hashables.previous, info.representative, info.open_block, ledger.balance (transaction, block_a.hashables.previous), vban::seconds_since_epoch (), info.block_count - 1, vban::epoch::epoch_0);
		ledger.update_account (transaction, destination_account, info, new_info);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, vban::pending_key (destination_account, block_a.hashables.source), { source_account, amount, vban::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, destination_account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (vban::stat::type::rollback, vban::stat::detail::receive);
	}
	void open_block (vban::open_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto amount (ledger.amount (transaction, hash));
		auto destination_account (ledger.account (transaction, hash));
		// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
		[[maybe_unused]] bool is_pruned (false);
		auto source_account (ledger.account_safe (transaction, block_a.hashables.source, is_pruned));
		ledger.cache.rep_weights.representation_add (block_a.representative (), 0 - amount);
		vban::account_info new_info;
		ledger.update_account (transaction, destination_account, new_info, new_info);
		ledger.store.block_del (transaction, hash);
		ledger.store.pending_put (transaction, vban::pending_key (destination_account, block_a.hashables.source), { source_account, amount, vban::epoch::epoch_0 });
		ledger.store.frontier_del (transaction, hash);
		ledger.stats.inc (vban::stat::type::rollback, vban::stat::detail::open);
	}
	void change_block (vban::change_block const & block_a) override
	{
		auto hash (block_a.hash ());
		auto rep_block (ledger.representative (transaction, block_a.hashables.previous));
		auto account (ledger.account (transaction, block_a.hashables.previous));
		vban::account_info info;
		[[maybe_unused]] auto error (ledger.store.account_get (transaction, account, info));
		debug_assert (!error);
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto block = ledger.store.block_get (transaction, rep_block);
		release_assert (block != nullptr);
		auto representative = block->representative ();
		ledger.cache.rep_weights.representation_add_dual (block_a.representative (), 0 - balance, representative, balance);
		ledger.store.block_del (transaction, hash);
		vban::account_info new_info (block_a.hashables.previous, representative, info.open_block, info.balance, vban::seconds_since_epoch (), info.block_count - 1, vban::epoch::epoch_0);
		ledger.update_account (transaction, account, info, new_info);
		ledger.store.frontier_del (transaction, hash);
		ledger.store.frontier_put (transaction, block_a.hashables.previous, account);
		ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
		ledger.stats.inc (vban::stat::type::rollback, vban::stat::detail::change);
	}
	void state_block (vban::state_block const & block_a) override
	{
		auto hash (block_a.hash ());
		vban::block_hash rep_block_hash (0);
		if (!block_a.hashables.previous.is_zero ())
		{
			rep_block_hash = ledger.representative (transaction, block_a.hashables.previous);
		}
		auto balance (ledger.balance (transaction, block_a.hashables.previous));
		auto is_send (block_a.hashables.balance < balance);
		vban::account representative{ 0 };
		if (!rep_block_hash.is_zero ())
		{
			// Move existing representation & add in amount delta
			auto block (ledger.store.block_get (transaction, rep_block_hash));
			debug_assert (block != nullptr);
			representative = block->representative ();
			ledger.cache.rep_weights.representation_add_dual (representative, balance, block_a.representative (), 0 - block_a.hashables.balance.number ());
		}
		else
		{
			// Add in amount delta only
			ledger.cache.rep_weights.representation_add (block_a.representative (), 0 - block_a.hashables.balance.number ());
		}

		vban::account_info info;
		auto error (ledger.store.account_get (transaction, block_a.hashables.account, info));

		if (is_send)
		{
			vban::pending_key key (block_a.hashables.link.as_account (), hash);
			while (!error && !ledger.store.pending_exists (transaction, key))
			{
				error = ledger.rollback (transaction, ledger.latest (transaction, block_a.hashables.link.as_account ()), list);
			}
			ledger.store.pending_del (transaction, key);
			ledger.stats.inc (vban::stat::type::rollback, vban::stat::detail::send);
		}
		else if (!block_a.hashables.link.is_zero () && !ledger.is_epoch_link (block_a.hashables.link))
		{
			// Pending account entry can be incorrect if source block was pruned. But it's not affecting correct ledger processing
			[[maybe_unused]] bool is_pruned (false);
			auto source_account (ledger.account_safe (transaction, block_a.hashables.link.as_block_hash (), is_pruned));
			vban::pending_info pending_info (source_account, block_a.hashables.balance.number () - balance, block_a.sideband ().source_epoch);
			ledger.store.pending_put (transaction, vban::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()), pending_info);
			ledger.stats.inc (vban::stat::type::rollback, vban::stat::detail::receive);
		}

		debug_assert (!error);
		auto previous_version (ledger.store.block_version (transaction, block_a.hashables.previous));
		vban::account_info new_info (block_a.hashables.previous, representative, info.open_block, balance, vban::seconds_since_epoch (), info.block_count - 1, previous_version);
		ledger.update_account (transaction, block_a.hashables.account, info, new_info);

		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		if (previous != nullptr)
		{
			ledger.store.block_successor_clear (transaction, block_a.hashables.previous);
			if (previous->type () < vban::block_type::state)
			{
				ledger.store.frontier_put (transaction, block_a.hashables.previous, block_a.hashables.account);
			}
		}
		else
		{
			ledger.stats.inc (vban::stat::type::rollback, vban::stat::detail::open);
		}
		ledger.store.block_del (transaction, hash);
	}
	vban::write_transaction const & transaction;
	vban::ledger & ledger;
	std::vector<std::shared_ptr<vban::block>> & list;
	bool error{ false };
};

class ledger_processor : public vban::mutable_block_visitor
{
public:
	ledger_processor (vban::ledger &, vban::write_transaction const &, vban::signature_verification = vban::signature_verification::unknown);
	virtual ~ledger_processor () = default;
	void send_block (vban::send_block &) override;
	void receive_block (vban::receive_block &) override;
	void open_block (vban::open_block &) override;
	void change_block (vban::change_block &) override;
	void state_block (vban::state_block &) override;
	void state_block_impl (vban::state_block &);
	void epoch_block_impl (vban::state_block &);
	vban::ledger & ledger;
	vban::write_transaction const & transaction;
	vban::signature_verification verification;
	vban::process_return result;

private:
	bool validate_epoch_block (vban::state_block const & block_a);
};

// Returns true if this block which has an epoch link is correctly formed.
bool ledger_processor::validate_epoch_block (vban::state_block const & block_a)
{
	debug_assert (ledger.is_epoch_link (block_a.hashables.link));
	vban::amount prev_balance (0);
	if (!block_a.hashables.previous.is_zero ())
	{
		result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? vban::process_result::progress : vban::process_result::gap_previous;
		if (result.code == vban::process_result::progress)
		{
			prev_balance = ledger.balance (transaction, block_a.hashables.previous);
		}
		else if (result.verified == vban::signature_verification::unknown)
		{
			// Check for possible regular state blocks with epoch link (send subtype)
			if (validate_message (block_a.hashables.account, block_a.hash (), block_a.signature))
			{
				// Is epoch block signed correctly
				if (validate_message (ledger.epoch_signer (block_a.link ()), block_a.hash (), block_a.signature))
				{
					result.verified = vban::signature_verification::invalid;
					result.code = vban::process_result::bad_signature;
				}
				else
				{
					result.verified = vban::signature_verification::valid_epoch;
				}
			}
			else
			{
				result.verified = vban::signature_verification::valid;
			}
		}
	}
	return (block_a.hashables.balance == prev_balance);
}

void ledger_processor::state_block (vban::state_block & block_a)
{
	result.code = vban::process_result::progress;
	auto is_epoch_block = false;
	if (ledger.is_epoch_link (block_a.hashables.link))
	{
		// This function also modifies the result variable if epoch is mal-formed
		is_epoch_block = validate_epoch_block (block_a);
	}

	if (result.code == vban::process_result::progress)
	{
		if (is_epoch_block)
		{
			epoch_block_impl (block_a);
		}
		else
		{
			state_block_impl (block_a);
		}
	}
}

void ledger_processor::state_block_impl (vban::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? vban::process_result::old : vban::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == vban::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != vban::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? vban::process_result::bad_signature : vban::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == vban::process_result::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = vban::signature_verification::valid;
			result.code = block_a.hashables.account.is_zero () ? vban::process_result::opened_burn_account : vban::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == vban::process_result::progress)
			{
				vban::epoch epoch (vban::epoch::epoch_0);
				vban::epoch source_epoch (vban::epoch::epoch_0);
				vban::account_info info;
				vban::amount amount (block_a.hashables.balance);
				auto is_send (false);
				auto is_receive (false);
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					epoch = info.epoch ();
					result.previous_balance = info.balance;
					result.code = block_a.hashables.previous.is_zero () ? vban::process_result::fork : vban::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == vban::process_result::progress)
					{
						result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? vban::process_result::progress : vban::process_result::gap_previous; // Does the previous block exist in the ledger? (Unambigious)
						if (result.code == vban::process_result::progress)
						{
							is_send = block_a.hashables.balance < info.balance;
							is_receive = !is_send && !block_a.hashables.link.is_zero ();
							amount = is_send ? (info.balance.number () - amount.number ()) : (amount.number () - info.balance.number ());
							result.code = block_a.hashables.previous == info.head ? vban::process_result::progress : vban::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						}
					}
				}
				else
				{
					// Account does not yet exists
					result.previous_balance = 0;
					result.code = block_a.previous ().is_zero () ? vban::process_result::progress : vban::process_result::gap_previous; // Does the first block in an account yield 0 for previous() ? (Unambigious)
					if (result.code == vban::process_result::progress)
					{
						is_receive = true;
						result.code = !block_a.hashables.link.is_zero () ? vban::process_result::progress : vban::process_result::gap_source; // Is the first block receiving from a send ? (Unambigious)
					}
				}
				if (result.code == vban::process_result::progress)
				{
					if (!is_send)
					{
						if (!block_a.hashables.link.is_zero ())
						{
							result.code = ledger.block_or_pruned_exists (transaction, block_a.hashables.link.as_block_hash ()) ? vban::process_result::progress : vban::process_result::gap_source; // Have we seen the source block already? (Harmless)
							if (result.code == vban::process_result::progress)
							{
								vban::pending_key key (block_a.hashables.account, block_a.hashables.link.as_block_hash ());
								vban::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? vban::process_result::unreceivable : vban::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == vban::process_result::progress)
								{
									result.code = amount == pending.amount ? vban::process_result::progress : vban::process_result::balance_mismatch;
									source_epoch = pending.epoch;
									epoch = std::max (epoch, source_epoch);
								}
							}
						}
						else
						{
							// If there's no link, the balance must remain the same, only the representative can change
							result.code = amount.is_zero () ? vban::process_result::progress : vban::process_result::balance_mismatch;
						}
					}
				}
				if (result.code == vban::process_result::progress)
				{
					vban::block_details block_details (epoch, is_send, is_receive, false);
					result.code = block_a.difficulty () >= vban::work_threshold (block_a.work_version (), block_details) ? vban::process_result::progress : vban::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
					if (result.code == vban::process_result::progress)
					{
						ledger.stats.inc (vban::stat::type::ledger, vban::stat::detail::state_block);
						block_a.sideband_set (vban::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, vban::seconds_since_epoch (), block_details, source_epoch));
						ledger.store.block_put (transaction, hash, block_a);

						if (!info.head.is_zero ())
						{
							// Move existing representation & add in amount delta
							ledger.cache.rep_weights.representation_add_dual (info.representative, 0 - info.balance.number (), block_a.representative (), block_a.hashables.balance.number ());
						}
						else
						{
							// Add in amount delta only
							ledger.cache.rep_weights.representation_add (block_a.representative (), block_a.hashables.balance.number ());
						}

						if (is_send)
						{
							vban::pending_key key (block_a.hashables.link.as_account (), hash);
							vban::pending_info info (block_a.hashables.account, amount.number (), epoch);
							ledger.store.pending_put (transaction, key, info);
						}
						else if (!block_a.hashables.link.is_zero ())
						{
							ledger.store.pending_del (transaction, vban::pending_key (block_a.hashables.account, block_a.hashables.link.as_block_hash ()));
						}

						vban::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, block_a.hashables.balance, vban::seconds_since_epoch (), info.block_count + 1, epoch);
						ledger.update_account (transaction, block_a.hashables.account, info, new_info);
						if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
						{
							ledger.store.frontier_del (transaction, info.head);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::epoch_block_impl (vban::state_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? vban::process_result::old : vban::process_result::progress; // Have we seen this block before? (Unambiguous)
	if (result.code == vban::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != vban::signature_verification::valid_epoch)
		{
			result.code = validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature) ? vban::process_result::bad_signature : vban::process_result::progress; // Is this block signed correctly (Unambiguous)
		}
		if (result.code == vban::process_result::progress)
		{
			debug_assert (!validate_message (ledger.epoch_signer (block_a.hashables.link), hash, block_a.signature));
			result.verified = vban::signature_verification::valid_epoch;
			result.code = block_a.hashables.account.is_zero () ? vban::process_result::opened_burn_account : vban::process_result::progress; // Is this for the burn account? (Unambiguous)
			if (result.code == vban::process_result::progress)
			{
				vban::account_info info;
				auto account_error (ledger.store.account_get (transaction, block_a.hashables.account, info));
				if (!account_error)
				{
					// Account already exists
					result.previous_balance = info.balance;
					result.code = block_a.hashables.previous.is_zero () ? vban::process_result::fork : vban::process_result::progress; // Has this account already been opened? (Ambigious)
					if (result.code == vban::process_result::progress)
					{
						result.code = block_a.hashables.previous == info.head ? vban::process_result::progress : vban::process_result::fork; // Is the previous block the account's head block? (Ambigious)
						if (result.code == vban::process_result::progress)
						{
							result.code = block_a.hashables.representative == info.representative ? vban::process_result::progress : vban::process_result::representative_mismatch;
						}
					}
				}
				else
				{
					result.previous_balance = 0;
					result.code = block_a.hashables.representative.is_zero () ? vban::process_result::progress : vban::process_result::representative_mismatch;
					// Non-exisitng account should have pending entries
					if (result.code == vban::process_result::progress)
					{
						bool pending_exists = ledger.store.pending_any (transaction, block_a.hashables.account);
						result.code = pending_exists ? vban::process_result::progress : vban::process_result::gap_epoch_open_pending;
					}
				}
				if (result.code == vban::process_result::progress)
				{
					auto epoch = ledger.network_params.ledger.epochs.epoch (block_a.hashables.link);
					// Must be an epoch for an unopened account or the epoch upgrade must be sequential
					auto is_valid_epoch_upgrade = account_error ? static_cast<std::underlying_type_t<vban::epoch>> (epoch) > 0 : vban::epochs::is_sequential (info.epoch (), epoch);
					result.code = is_valid_epoch_upgrade ? vban::process_result::progress : vban::process_result::block_position;
					if (result.code == vban::process_result::progress)
					{
						result.code = block_a.hashables.balance == info.balance ? vban::process_result::progress : vban::process_result::balance_mismatch;
						if (result.code == vban::process_result::progress)
						{
							vban::block_details block_details (epoch, false, false, true);
							result.code = block_a.difficulty () >= vban::work_threshold (block_a.work_version (), block_details) ? vban::process_result::progress : vban::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
							if (result.code == vban::process_result::progress)
							{
								ledger.stats.inc (vban::stat::type::ledger, vban::stat::detail::epoch_block);
								block_a.sideband_set (vban::block_sideband (block_a.hashables.account /* unused */, 0, 0 /* unused */, info.block_count + 1, vban::seconds_since_epoch (), block_details, vban::epoch::epoch_0 /* unused */));
								ledger.store.block_put (transaction, hash, block_a);
								vban::account_info new_info (hash, block_a.representative (), info.open_block.is_zero () ? hash : info.open_block, info.balance, vban::seconds_since_epoch (), info.block_count + 1, epoch);
								ledger.update_account (transaction, block_a.hashables.account, info, new_info);
								if (!ledger.store.frontier_get (transaction, info.head).is_zero ())
								{
									ledger.store.frontier_del (transaction, info.head);
								}
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::change_block (vban::change_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? vban::process_result::old : vban::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == vban::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? vban::process_result::progress : vban::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == vban::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? vban::process_result::progress : vban::process_result::block_position;
			if (result.code == vban::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? vban::process_result::fork : vban::process_result::progress;
				if (result.code == vban::process_result::progress)
				{
					vban::account_info info;
					auto latest_error (ledger.store.account_get (transaction, account, info));
					(void)latest_error;
					debug_assert (!latest_error);
					debug_assert (info.head == block_a.hashables.previous);
					// Validate block if not verified outside of ledger
					if (result.verified != vban::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? vban::process_result::bad_signature : vban::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == vban::process_result::progress)
					{
						vban::block_details block_details (vban::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result.code = block_a.difficulty () >= vban::work_threshold (block_a.work_version (), block_details) ? vban::process_result::progress : vban::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result.code == vban::process_result::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							result.verified = vban::signature_verification::valid;
							block_a.sideband_set (vban::block_sideband (account, 0, info.balance, info.block_count + 1, vban::seconds_since_epoch (), block_details, vban::epoch::epoch_0 /* unused */));
							ledger.store.block_put (transaction, hash, block_a);
							auto balance (ledger.balance (transaction, block_a.hashables.previous));
							ledger.cache.rep_weights.representation_add_dual (block_a.representative (), balance, info.representative, 0 - balance);
							vban::account_info new_info (hash, block_a.representative (), info.open_block, info.balance, vban::seconds_since_epoch (), info.block_count + 1, vban::epoch::epoch_0);
							ledger.update_account (transaction, account, info, new_info);
							ledger.store.frontier_del (transaction, block_a.hashables.previous);
							ledger.store.frontier_put (transaction, hash, account);
							result.previous_balance = info.balance;
							ledger.stats.inc (vban::stat::type::ledger, vban::stat::detail::change);
						}
					}
				}
			}
		}
	}
}

void ledger_processor::send_block (vban::send_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? vban::process_result::old : vban::process_result::progress; // Have we seen this block before? (Harmless)
	if (result.code == vban::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? vban::process_result::progress : vban::process_result::gap_previous; // Have we seen the previous block already? (Harmless)
		if (result.code == vban::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? vban::process_result::progress : vban::process_result::block_position;
			if (result.code == vban::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? vban::process_result::fork : vban::process_result::progress;
				if (result.code == vban::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != vban::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? vban::process_result::bad_signature : vban::process_result::progress; // Is this block signed correctly (Malformed)
					}
					if (result.code == vban::process_result::progress)
					{
						vban::block_details block_details (vban::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
						result.code = block_a.difficulty () >= vban::work_threshold (block_a.work_version (), block_details) ? vban::process_result::progress : vban::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
						if (result.code == vban::process_result::progress)
						{
							debug_assert (!validate_message (account, hash, block_a.signature));
							result.verified = vban::signature_verification::valid;
							vban::account_info info;
							auto latest_error (ledger.store.account_get (transaction, account, info));
							(void)latest_error;
							debug_assert (!latest_error);
							debug_assert (info.head == block_a.hashables.previous);
							result.code = info.balance.number () >= block_a.hashables.balance.number () ? vban::process_result::progress : vban::process_result::negative_spend; // Is this trying to spend a negative amount (Malicious)
							if (result.code == vban::process_result::progress)
							{
								auto amount (info.balance.number () - block_a.hashables.balance.number ());
								ledger.cache.rep_weights.representation_add (info.representative, 0 - amount);
								block_a.sideband_set (vban::block_sideband (account, 0, block_a.hashables.balance /* unused */, info.block_count + 1, vban::seconds_since_epoch (), block_details, vban::epoch::epoch_0 /* unused */));
								ledger.store.block_put (transaction, hash, block_a);
								vban::account_info new_info (hash, info.representative, info.open_block, block_a.hashables.balance, vban::seconds_since_epoch (), info.block_count + 1, vban::epoch::epoch_0);
								ledger.update_account (transaction, account, info, new_info);
								ledger.store.pending_put (transaction, vban::pending_key (block_a.hashables.destination, hash), { account, amount, vban::epoch::epoch_0 });
								ledger.store.frontier_del (transaction, block_a.hashables.previous);
								ledger.store.frontier_put (transaction, hash, account);
								result.previous_balance = info.balance;
								ledger.stats.inc (vban::stat::type::ledger, vban::stat::detail::send);
							}
						}
					}
				}
			}
		}
	}
}

void ledger_processor::receive_block (vban::receive_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? vban::process_result::old : vban::process_result::progress; // Have we seen this block already?  (Harmless)
	if (result.code == vban::process_result::progress)
	{
		auto previous (ledger.store.block_get (transaction, block_a.hashables.previous));
		result.code = previous != nullptr ? vban::process_result::progress : vban::process_result::gap_previous;
		if (result.code == vban::process_result::progress)
		{
			result.code = block_a.valid_predecessor (*previous) ? vban::process_result::progress : vban::process_result::block_position;
			if (result.code == vban::process_result::progress)
			{
				auto account (ledger.store.frontier_get (transaction, block_a.hashables.previous));
				result.code = account.is_zero () ? vban::process_result::gap_previous : vban::process_result::progress; //Have we seen the previous block? No entries for account at all (Harmless)
				if (result.code == vban::process_result::progress)
				{
					// Validate block if not verified outside of ledger
					if (result.verified != vban::signature_verification::valid)
					{
						result.code = validate_message (account, hash, block_a.signature) ? vban::process_result::bad_signature : vban::process_result::progress; // Is the signature valid (Malformed)
					}
					if (result.code == vban::process_result::progress)
					{
						debug_assert (!validate_message (account, hash, block_a.signature));
						result.verified = vban::signature_verification::valid;
						result.code = ledger.block_or_pruned_exists (transaction, block_a.hashables.source) ? vban::process_result::progress : vban::process_result::gap_source; // Have we seen the source block already? (Harmless)
						if (result.code == vban::process_result::progress)
						{
							vban::account_info info;
							ledger.store.account_get (transaction, account, info);
							result.code = info.head == block_a.hashables.previous ? vban::process_result::progress : vban::process_result::gap_previous; // Block doesn't immediately follow latest block (Harmless)
							if (result.code == vban::process_result::progress)
							{
								vban::pending_key key (account, block_a.hashables.source);
								vban::pending_info pending;
								result.code = ledger.store.pending_get (transaction, key, pending) ? vban::process_result::unreceivable : vban::process_result::progress; // Has this source already been received (Malformed)
								if (result.code == vban::process_result::progress)
								{
									result.code = pending.epoch == vban::epoch::epoch_0 ? vban::process_result::progress : vban::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
									if (result.code == vban::process_result::progress)
									{
										vban::block_details block_details (vban::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
										result.code = block_a.difficulty () >= vban::work_threshold (block_a.work_version (), block_details) ? vban::process_result::progress : vban::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
										if (result.code == vban::process_result::progress)
										{
											auto new_balance (info.balance.number () + pending.amount.number ());
#ifdef NDEBUG
											if (ledger.store.block_exists (transaction, block_a.hashables.source))
											{
												vban::account_info source_info;
												[[maybe_unused]] auto error (ledger.store.account_get (transaction, pending.source, source_info));
												debug_assert (!error);
											}
#endif
											ledger.store.pending_del (transaction, key);
											block_a.sideband_set (vban::block_sideband (account, 0, new_balance, info.block_count + 1, vban::seconds_since_epoch (), block_details, vban::epoch::epoch_0 /* unused */));
											ledger.store.block_put (transaction, hash, block_a);
											vban::account_info new_info (hash, info.representative, info.open_block, new_balance, vban::seconds_since_epoch (), info.block_count + 1, vban::epoch::epoch_0);
											ledger.update_account (transaction, account, info, new_info);
											ledger.cache.rep_weights.representation_add (info.representative, pending.amount.number ());
											ledger.store.frontier_del (transaction, block_a.hashables.previous);
											ledger.store.frontier_put (transaction, hash, account);
											result.previous_balance = info.balance;
											ledger.stats.inc (vban::stat::type::ledger, vban::stat::detail::receive);
										}
									}
								}
							}
						}
					}
				}
				else
				{
					result.code = ledger.store.block_exists (transaction, block_a.hashables.previous) ? vban::process_result::fork : vban::process_result::gap_previous; // If we have the block but it's not the latest we have a signed fork (Malicious)
				}
			}
		}
	}
}

void ledger_processor::open_block (vban::open_block & block_a)
{
	auto hash (block_a.hash ());
	auto existing (ledger.block_or_pruned_exists (transaction, hash));
	result.code = existing ? vban::process_result::old : vban::process_result::progress; // Have we seen this block already? (Harmless)
	if (result.code == vban::process_result::progress)
	{
		// Validate block if not verified outside of ledger
		if (result.verified != vban::signature_verification::valid)
		{
			result.code = validate_message (block_a.hashables.account, hash, block_a.signature) ? vban::process_result::bad_signature : vban::process_result::progress; // Is the signature valid (Malformed)
		}
		if (result.code == vban::process_result::progress)
		{
			debug_assert (!validate_message (block_a.hashables.account, hash, block_a.signature));
			result.verified = vban::signature_verification::valid;
			result.code = ledger.block_or_pruned_exists (transaction, block_a.hashables.source) ? vban::process_result::progress : vban::process_result::gap_source; // Have we seen the source block? (Harmless)
			if (result.code == vban::process_result::progress)
			{
				vban::account_info info;
				result.code = ledger.store.account_get (transaction, block_a.hashables.account, info) ? vban::process_result::progress : vban::process_result::fork; // Has this account already been opened? (Malicious)
				if (result.code == vban::process_result::progress)
				{
					vban::pending_key key (block_a.hashables.account, block_a.hashables.source);
					vban::pending_info pending;
					result.code = ledger.store.pending_get (transaction, key, pending) ? vban::process_result::unreceivable : vban::process_result::progress; // Has this source already been received (Malformed)
					if (result.code == vban::process_result::progress)
					{
						result.code = block_a.hashables.account == ledger.network_params.ledger.burn_account ? vban::process_result::opened_burn_account : vban::process_result::progress; // Is it burning 0 account? (Malicious)
						if (result.code == vban::process_result::progress)
						{
							result.code = pending.epoch == vban::epoch::epoch_0 ? vban::process_result::progress : vban::process_result::unreceivable; // Are we receiving a state-only send? (Malformed)
							if (result.code == vban::process_result::progress)
							{
								vban::block_details block_details (vban::epoch::epoch_0, false /* unused */, false /* unused */, false /* unused */);
								result.code = block_a.difficulty () >= vban::work_threshold (block_a.work_version (), block_details) ? vban::process_result::progress : vban::process_result::insufficient_work; // Does this block have sufficient work? (Malformed)
								if (result.code == vban::process_result::progress)
								{
#ifdef NDEBUG
									if (ledger.store.block_exists (transaction, block_a.hashables.source))
									{
										vban::account_info source_info;
										[[maybe_unused]] auto error (ledger.store.account_get (transaction, pending.source, source_info));
										debug_assert (!error);
									}
#endif
									ledger.store.pending_del (transaction, key);
									block_a.sideband_set (vban::block_sideband (block_a.hashables.account, 0, pending.amount, 1, vban::seconds_since_epoch (), block_details, vban::epoch::epoch_0 /* unused */));
									ledger.store.block_put (transaction, hash, block_a);
									vban::account_info new_info (hash, block_a.representative (), hash, pending.amount.number (), vban::seconds_since_epoch (), 1, vban::epoch::epoch_0);
									ledger.update_account (transaction, block_a.hashables.account, info, new_info);
									ledger.cache.rep_weights.representation_add (block_a.representative (), pending.amount.number ());
									ledger.store.frontier_put (transaction, hash, block_a.hashables.account);
									result.previous_balance = 0;
									ledger.stats.inc (vban::stat::type::ledger, vban::stat::detail::open);
								}
							}
						}
					}
				}
			}
		}
	}
}

ledger_processor::ledger_processor (vban::ledger & ledger_a, vban::write_transaction const & transaction_a, vban::signature_verification verification_a) :
	ledger (ledger_a),
	transaction (transaction_a),
	verification (verification_a)
{
	result.verified = verification;
}
} // namespace

vban::ledger::ledger (vban::block_store & store_a, vban::stat & stat_a, vban::generate_cache const & generate_cache_a) :
	store (store_a),
	stats (stat_a),
	check_bootstrap_weights (true)
{
	if (!store.init_error ())
	{
		initialize (generate_cache_a);
	}
}

void vban::ledger::initialize (vban::generate_cache const & generate_cache_a)
{
	if (generate_cache_a.reps || generate_cache_a.account_count || generate_cache_a.block_count)
	{
		store.accounts_for_each_par (
		[this] (vban::read_transaction const & /*unused*/, vban::store_iterator<vban::account, vban::account_info> i, vban::store_iterator<vban::account, vban::account_info> n) {
			uint64_t block_count_l{ 0 };
			uint64_t account_count_l{ 0 };
			decltype (this->cache.rep_weights) rep_weights_l;
			for (; i != n; ++i)
			{
				vban::account_info const & info (i->second);
				block_count_l += info.block_count;
				++account_count_l;
				rep_weights_l.representation_add (info.representative, info.balance.number ());
			}
			this->cache.block_count += block_count_l;
			this->cache.account_count += account_count_l;
			this->cache.rep_weights.copy_from (rep_weights_l);
		});
	}

	if (generate_cache_a.cemented_count)
	{
		store.confirmation_height_for_each_par (
		[this] (vban::read_transaction const & /*unused*/, vban::store_iterator<vban::account, vban::confirmation_height_info> i, vban::store_iterator<vban::account, vban::confirmation_height_info> n) {
			uint64_t cemented_count_l (0);
			for (; i != n; ++i)
			{
				cemented_count_l += i->second.height;
			}
			this->cache.cemented_count += cemented_count_l;
		});
	}

	auto transaction (store.tx_begin_read ());
	cache.pruned_count = store.pruned_count (transaction);

	// Final votes requirement for confirmation canary block
	vban::confirmation_height_info confirmation_height_info;
	if (!store.confirmation_height_get (transaction, network_params.ledger.final_votes_canary_account, confirmation_height_info))
	{
		cache.final_votes_confirmation_canary = (confirmation_height_info.height >= network_params.ledger.final_votes_canary_height);
	}
}

// Balance for account containing hash
vban::uint256_t vban::ledger::balance (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const
{
	return hash_a.is_zero () ? 0 : store.block_balance (transaction_a, hash_a);
}

vban::uint256_t vban::ledger::balance_safe (vban::transaction const & transaction_a, vban::block_hash const & hash_a, bool & error_a) const
{
	vban::uint256_t result (0);
	if (pruning && !hash_a.is_zero () && !store.block_exists (transaction_a, hash_a))
	{
		error_a = true;
		result = 0;
	}
	else
	{
		result = balance (transaction_a, hash_a);
	}
	return result;
}

// Balance for an account by account number
vban::uint256_t vban::ledger::account_balance (vban::transaction const & transaction_a, vban::account const & account_a, bool only_confirmed_a)
{
	vban::uint256_t result (0);
	if (only_confirmed_a)
	{
		vban::confirmation_height_info info;
		if (!store.confirmation_height_get (transaction_a, account_a, info))
		{
			result = balance (transaction_a, info.frontier);
		}
	}
	else
	{
		vban::account_info info;
		auto none (store.account_get (transaction_a, account_a, info));
		if (!none)
		{
			result = info.balance.number ();
		}
	}
	return result;
}

vban::uint256_t vban::ledger::account_pending (vban::transaction const & transaction_a, vban::account const & account_a, bool only_confirmed_a)
{
	vban::uint256_t result (0);
	vban::account end (account_a.number () + 1);
	for (auto i (store.pending_begin (transaction_a, vban::pending_key (account_a, 0))), n (store.pending_begin (transaction_a, vban::pending_key (end, 0))); i != n; ++i)
	{
		vban::pending_info const & info (i->second);
		if (only_confirmed_a)
		{
			if (block_confirmed (transaction_a, i->first.hash))
			{
				result += info.amount.number ();
			}
		}
		else
		{
			result += info.amount.number ();
		}
	}
	return result;
}

vban::process_return vban::ledger::process (vban::write_transaction const & transaction_a, vban::block & block_a, vban::signature_verification verification)
{
	debug_assert (!vban::work_validate_entry (block_a) || network_params.network.is_dev_network ());
	ledger_processor processor (*this, transaction_a, verification);
	block_a.visit (processor);
	if (processor.result.code == vban::process_result::progress)
	{
		++cache.block_count;
	}
	return processor.result;
}

vban::block_hash vban::ledger::representative (vban::transaction const & transaction_a, vban::block_hash const & hash_a)
{
	auto result (representative_calculated (transaction_a, hash_a));
	debug_assert (result.is_zero () || store.block_exists (transaction_a, result));
	return result;
}

vban::block_hash vban::ledger::representative_calculated (vban::transaction const & transaction_a, vban::block_hash const & hash_a)
{
	representative_visitor visitor (transaction_a, store);
	visitor.compute (hash_a);
	return visitor.result;
}

bool vban::ledger::block_or_pruned_exists (vban::block_hash const & hash_a) const
{
	return block_or_pruned_exists (store.tx_begin_read (), hash_a);
}

bool vban::ledger::block_or_pruned_exists (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const
{
	if (store.pruned_exists (transaction_a, hash_a))
	{
		return true;
	}
	return store.block_exists (transaction_a, hash_a);
}

std::string vban::ledger::block_text (char const * hash_a)
{
	return block_text (vban::block_hash (hash_a));
}

std::string vban::ledger::block_text (vban::block_hash const & hash_a)
{
	std::string result;
	auto transaction (store.tx_begin_read ());
	auto block (store.block_get (transaction, hash_a));
	if (block != nullptr)
	{
		block->serialize_json (result);
	}
	return result;
}

bool vban::ledger::is_send (vban::transaction const & transaction_a, vban::state_block const & block_a) const
{
	/*
	 * if block_a does not have a sideband, then is_send()
	 * requires that the previous block exists in the database.
	 * This is because it must retrieve the balance of the previous block.
	 */
	debug_assert (block_a.has_sideband () || block_a.hashables.previous.is_zero () || store.block_exists (transaction_a, block_a.hashables.previous));

	bool result (false);
	if (block_a.has_sideband ())
	{
		result = block_a.sideband ().details.is_send;
	}
	else
	{
		vban::block_hash previous (block_a.hashables.previous);
		if (!previous.is_zero ())
		{
			if (block_a.hashables.balance < balance (transaction_a, previous))
			{
				result = true;
			}
		}
	}
	return result;
}

vban::account const & vban::ledger::block_destination (vban::transaction const & transaction_a, vban::block const & block_a)
{
	vban::send_block const * send_block (dynamic_cast<vban::send_block const *> (&block_a));
	vban::state_block const * state_block (dynamic_cast<vban::state_block const *> (&block_a));
	if (send_block != nullptr)
	{
		return send_block->hashables.destination;
	}
	else if (state_block != nullptr && is_send (transaction_a, *state_block))
	{
		return state_block->hashables.link.as_account ();
	}
	static vban::account result (0);
	return result;
}

vban::block_hash vban::ledger::block_source (vban::transaction const & transaction_a, vban::block const & block_a)
{
	/*
	 * block_source() requires that the previous block of the block
	 * passed in exist in the database.  This is because it will try
	 * to check account balances to determine if it is a send block.
	 */
	debug_assert (block_a.previous ().is_zero () || store.block_exists (transaction_a, block_a.previous ()));

	// If block_a.source () is nonzero, then we have our source.
	// However, universal blocks will always return zero.
	vban::block_hash result (block_a.source ());
	vban::state_block const * state_block (dynamic_cast<vban::state_block const *> (&block_a));
	if (state_block != nullptr && !is_send (transaction_a, *state_block))
	{
		result = state_block->hashables.link.as_block_hash ();
	}
	return result;
}

std::pair<vban::block_hash, vban::block_hash> vban::ledger::hash_root_random (vban::transaction const & transaction_a) const
{
	vban::block_hash hash (0);
	vban::root root (0);
	if (!pruning)
	{
		auto block (store.block_random (transaction_a));
		hash = block->hash ();
		root = block->root ();
	}
	else
	{
		uint64_t count (cache.block_count);
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > count);
		auto region = static_cast<size_t> (vban::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (count - 1)));
		// Pruned cache cannot guarantee that pruned blocks are already commited
		if (region < cache.pruned_count)
		{
			hash = store.pruned_random (transaction_a);
		}
		if (hash.is_zero ())
		{
			auto block (store.block_random (transaction_a));
			hash = block->hash ();
			root = block->root ();
		}
	}
	return std::make_pair (hash, root.as_block_hash ());
}

// Vote weight of an account
vban::uint256_t vban::ledger::weight (vban::account const & account_a)
{
	if (check_bootstrap_weights.load ())
	{
		if (cache.block_count < bootstrap_weight_max_blocks)
		{
			auto weight = bootstrap_weights.find (account_a);
			if (weight != bootstrap_weights.end ())
			{
				return weight->second;
			}
		}
		else
		{
			check_bootstrap_weights = false;
		}
	}
	return cache.rep_weights.representation_get (account_a);
}

// Rollback blocks until `block_a' doesn't exist or it tries to penetrate the confirmation height
bool vban::ledger::rollback (vban::write_transaction const & transaction_a, vban::block_hash const & block_a, std::vector<std::shared_ptr<vban::block>> & list_a)
{
	debug_assert (store.block_exists (transaction_a, block_a));
	auto account_l (account (transaction_a, block_a));
	auto block_account_height (store.block_account_height (transaction_a, block_a));
	rollback_visitor rollback (transaction_a, *this, list_a);
	vban::account_info account_info;
	auto error (false);
	while (!error && store.block_exists (transaction_a, block_a))
	{
		vban::confirmation_height_info confirmation_height_info;
		store.confirmation_height_get (transaction_a, account_l, confirmation_height_info);
		if (block_account_height > confirmation_height_info.height)
		{
			auto latest_error = store.account_get (transaction_a, account_l, account_info);
			debug_assert (!latest_error);
			auto block (store.block_get (transaction_a, account_info.head));
			list_a.push_back (block);
			block->visit (rollback);
			error = rollback.error;
			if (!error)
			{
				--cache.block_count;
			}
		}
		else
		{
			error = true;
		}
	}
	return error;
}

bool vban::ledger::rollback (vban::write_transaction const & transaction_a, vban::block_hash const & block_a)
{
	std::vector<std::shared_ptr<vban::block>> rollback_list;
	return rollback (transaction_a, block_a, rollback_list);
}

// Return account containing hash
vban::account vban::ledger::account (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const
{
	return store.block_account (transaction_a, hash_a);
}

vban::account vban::ledger::account_safe (vban::transaction const & transaction_a, vban::block_hash const & hash_a, bool & error_a) const
{
	if (!pruning)
	{
		return store.block_account (transaction_a, hash_a);
	}
	else
	{
		auto block (store.block_get (transaction_a, hash_a));
		if (block != nullptr)
		{
			return store.block_account_calculated (*block);
		}
		else
		{
			error_a = true;
			return 0;
		}
	}
}

// Return amount decrease or increase for block
vban::uint256_t vban::ledger::amount (vban::transaction const & transaction_a, vban::account const & account_a)
{
	release_assert (account_a == network_params.ledger.genesis_account);
	return network_params.ledger.genesis_amount;
}

vban::uint256_t vban::ledger::amount (vban::transaction const & transaction_a, vban::block_hash const & hash_a)
{
	auto block (store.block_get (transaction_a, hash_a));
	auto block_balance (balance (transaction_a, hash_a));
	auto previous_balance (balance (transaction_a, block->previous ()));
	return block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
}

vban::uint256_t vban::ledger::amount_safe (vban::transaction const & transaction_a, vban::block_hash const & hash_a, bool & error_a) const
{
	auto block (store.block_get (transaction_a, hash_a));
	debug_assert (block);
	auto block_balance (balance (transaction_a, hash_a));
	auto previous_balance (balance_safe (transaction_a, block->previous (), error_a));
	return error_a ? 0 : block_balance > previous_balance ? block_balance - previous_balance : previous_balance - block_balance;
}

// Return latest block for account
vban::block_hash vban::ledger::latest (vban::transaction const & transaction_a, vban::account const & account_a)
{
	vban::account_info info;
	auto latest_error (store.account_get (transaction_a, account_a, info));
	return latest_error ? 0 : info.head;
}

// Return latest root for account, account number if there are no blocks for this account.
vban::root vban::ledger::latest_root (vban::transaction const & transaction_a, vban::account const & account_a)
{
	vban::account_info info;
	if (store.account_get (transaction_a, account_a, info))
	{
		return account_a;
	}
	else
	{
		return info.head;
	}
}

void vban::ledger::dump_account_chain (vban::account const & account_a, std::ostream & stream)
{
	auto transaction (store.tx_begin_read ());
	auto hash (latest (transaction, account_a));
	while (!hash.is_zero ())
	{
		auto block (store.block_get (transaction, hash));
		debug_assert (block != nullptr);
		stream << hash.to_string () << std::endl;
		hash = block->previous ();
	}
}

bool vban::ledger::could_fit (vban::transaction const & transaction_a, vban::block const & block_a) const
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a] (vban::block_hash const & hash_a) {
		return hash_a.is_zero () || store.block_exists (transaction_a, hash_a);
	});
}

bool vban::ledger::dependents_confirmed (vban::transaction const & transaction_a, vban::block const & block_a) const
{
	auto dependencies (dependent_blocks (transaction_a, block_a));
	return std::all_of (dependencies.begin (), dependencies.end (), [this, &transaction_a] (vban::block_hash const & hash_a) {
		auto result (hash_a.is_zero ());
		if (!result)
		{
			result = block_confirmed (transaction_a, hash_a);
		}
		return result;
	});
}

bool vban::ledger::is_epoch_link (vban::link const & link_a) const
{
	return network_params.ledger.epochs.is_epoch_link (link_a);
}

class dependent_block_visitor : public vban::block_visitor
{
public:
	dependent_block_visitor (vban::ledger const & ledger_a, vban::transaction const & transaction_a) :
		ledger (ledger_a),
		transaction (transaction_a),
		result ({ 0, 0 })
	{
	}
	void send_block (vban::send_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void receive_block (vban::receive_block const & block_a) override
	{
		result[0] = block_a.previous ();
		result[1] = block_a.source ();
	}
	void open_block (vban::open_block const & block_a) override
	{
		if (block_a.source () != ledger.network_params.ledger.genesis_account)
		{
			result[0] = block_a.source ();
		}
	}
	void change_block (vban::change_block const & block_a) override
	{
		result[0] = block_a.previous ();
	}
	void state_block (vban::state_block const & block_a) override
	{
		result[0] = block_a.hashables.previous;
		result[1] = block_a.hashables.link.as_block_hash ();
		// ledger.is_send will check the sideband first, if block_a has a loaded sideband the check that previous block exists can be skipped
		if (ledger.is_epoch_link (block_a.hashables.link) || ((block_a.has_sideband () || ledger.store.block_exists (transaction, block_a.hashables.previous)) && ledger.is_send (transaction, block_a)))
		{
			result[1].clear ();
		}
	}
	vban::ledger const & ledger;
	vban::transaction const & transaction;
	std::array<vban::block_hash, 2> result;
};

std::array<vban::block_hash, 2> vban::ledger::dependent_blocks (vban::transaction const & transaction_a, vban::block const & block_a) const
{
	dependent_block_visitor visitor (*this, transaction_a);
	block_a.visit (visitor);
	return visitor.result;
}

vban::account const & vban::ledger::epoch_signer (vban::link const & link_a) const
{
	return network_params.ledger.epochs.signer (network_params.ledger.epochs.epoch (link_a));
}

vban::link const & vban::ledger::epoch_link (vban::epoch epoch_a) const
{
	return network_params.ledger.epochs.link (epoch_a);
}

void vban::ledger::update_account (vban::write_transaction const & transaction_a, vban::account const & account_a, vban::account_info const & old_a, vban::account_info const & new_a)
{
	if (!new_a.head.is_zero ())
	{
		if (old_a.head.is_zero () && new_a.open_block == new_a.head)
		{
			++cache.account_count;
		}
		if (!old_a.head.is_zero () && old_a.epoch () != new_a.epoch ())
		{
			// store.account_put won't erase existing entries if they're in different tables
			store.account_del (transaction_a, account_a);
		}
		store.account_put (transaction_a, account_a, new_a);
	}
	else
	{
		debug_assert (!store.confirmation_height_exists (transaction_a, account_a));
		store.account_del (transaction_a, account_a);
		debug_assert (cache.account_count > 0);
		--cache.account_count;
	}
}

std::shared_ptr<vban::block> vban::ledger::successor (vban::transaction const & transaction_a, vban::qualified_root const & root_a)
{
	vban::block_hash successor (0);
	auto get_from_previous = false;
	if (root_a.previous ().is_zero ())
	{
		vban::account_info info;
		if (!store.account_get (transaction_a, root_a.root ().as_account (), info))
		{
			successor = info.open_block;
		}
		else
		{
			get_from_previous = true;
		}
	}
	else
	{
		get_from_previous = true;
	}

	if (get_from_previous)
	{
		successor = store.block_successor (transaction_a, root_a.previous ());
	}
	std::shared_ptr<vban::block> result;
	if (!successor.is_zero ())
	{
		result = store.block_get (transaction_a, successor);
	}
	debug_assert (successor.is_zero () || result != nullptr);
	return result;
}

std::shared_ptr<vban::block> vban::ledger::forked_block (vban::transaction const & transaction_a, vban::block const & block_a)
{
	debug_assert (!store.block_exists (transaction_a, block_a.hash ()));
	auto root (block_a.root ());
	debug_assert (store.block_exists (transaction_a, root.as_block_hash ()) || store.account_exists (transaction_a, root.as_account ()));
	auto result (store.block_get (transaction_a, store.block_successor (transaction_a, root.as_block_hash ())));
	if (result == nullptr)
	{
		vban::account_info info;
		auto error (store.account_get (transaction_a, root.as_account (), info));
		(void)error;
		debug_assert (!error);
		result = store.block_get (transaction_a, info.open_block);
		debug_assert (result != nullptr);
	}
	return result;
}

bool vban::ledger::block_confirmed (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const
{
	if (store.pruned_exists (transaction_a, hash_a))
	{
		return true;
	}
	auto block = store.block_get (transaction_a, hash_a);
	if (block)
	{
		vban::confirmation_height_info confirmation_height_info;
		store.confirmation_height_get (transaction_a, block->account ().is_zero () ? block->sideband ().account : block->account (), confirmation_height_info);
		auto confirmed (confirmation_height_info.height >= block->sideband ().height);
		return confirmed;
	}
	return false;
}

uint64_t vban::ledger::pruning_action (vban::write_transaction & transaction_a, vban::block_hash const & hash_a, uint64_t const batch_size_a)
{
	uint64_t pruned_count (0);
	vban::block_hash hash (hash_a);
	while (!hash.is_zero () && hash != network_params.ledger.genesis_hash)
	{
		auto block (store.block_get (transaction_a, hash));
		if (block != nullptr)
		{
			store.block_del (transaction_a, hash);
			store.pruned_put (transaction_a, hash);
			hash = block->previous ();
			++pruned_count;
			++cache.pruned_count;
			if (pruned_count % batch_size_a == 0)
			{
				transaction_a.commit ();
				transaction_a.renew ();
			}
		}
		else if (store.pruned_exists (transaction_a, hash))
		{
			hash = 0;
		}
		else
		{
			hash = 0;
			release_assert (false && "Error finding block for pruning");
		}
	}
	return pruned_count;
}

std::multimap<uint64_t, vban::uncemented_info, std::greater<>> vban::ledger::unconfirmed_frontiers () const
{
	vban::locked<std::multimap<uint64_t, vban::uncemented_info, std::greater<>>> result;
	using result_t = decltype (result)::value_type;

	store.accounts_for_each_par ([this, &result] (vban::read_transaction const & transaction_a, vban::store_iterator<vban::account, vban::account_info> i, vban::store_iterator<vban::account, vban::account_info> n) {
		result_t unconfirmed_frontiers_l;
		for (; i != n; ++i)
		{
			auto const & account (i->first);
			auto const & account_info (i->second);

			vban::confirmation_height_info conf_height_info;
			this->store.confirmation_height_get (transaction_a, account, conf_height_info);

			if (account_info.block_count != conf_height_info.height)
			{
				// Always output as no confirmation height has been set on the account yet
				auto height_delta = account_info.block_count - conf_height_info.height;
				auto const & frontier = account_info.head;
				auto const & cemented_frontier = conf_height_info.frontier;
				unconfirmed_frontiers_l.emplace (std::piecewise_construct, std::forward_as_tuple (height_delta), std::forward_as_tuple (cemented_frontier, frontier, i->first));
			}
		}
		// Merge results
		auto result_locked = result.lock ();
		result_locked->insert (unconfirmed_frontiers_l.begin (), unconfirmed_frontiers_l.end ());
	});
	return result;
}

// A precondition is that the store is an LMDB store
bool vban::ledger::migrate_lmdb_to_rocksdb (boost::filesystem::path const & data_path_a) const
{
	boost::system::error_code error_chmod;
	vban::set_secure_perm_directory (data_path_a, error_chmod);
	auto rockdb_data_path = data_path_a / "rocksdb";
	boost::filesystem::remove_all (rockdb_data_path);

	vban::logger_mt logger;
	auto error (false);

	// Open rocksdb database
	vban::rocksdb_config rocksdb_config;
	rocksdb_config.enable = true;
	auto rocksdb_store = vban::make_store (logger, data_path_a, false, true, rocksdb_config);

	if (!rocksdb_store->init_error ())
	{
		store.blocks_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::blocks }));

				std::vector<uint8_t> vector;
				{
					vban::vectorstream stream (vector);
					vban::serialize_block (stream, *i->second.block);
					i->second.sideband.serialize (stream, i->second.block->type ());
				}
				rocksdb_store->block_raw_put (rocksdb_transaction, vector, i->first);
			}
		});

		store.unchecked_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::unchecked }));
				rocksdb_store->unchecked_put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.pending_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::pending }));
				rocksdb_store->pending_put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.confirmation_height_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::confirmation_height }));
				rocksdb_store->confirmation_height_put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.accounts_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::accounts }));
				rocksdb_store->account_put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.frontiers_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::frontiers }));
				rocksdb_store->frontier_put (rocksdb_transaction, i->first, i->second);
			}
		});

		store.pruned_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::pruned }));
				rocksdb_store->pruned_put (rocksdb_transaction, i->first);
			}
		});

		store.final_vote_for_each_par (
		[&rocksdb_store] (vban::read_transaction const & /*unused*/, auto i, auto n) {
			for (; i != n; ++i)
			{
				auto rocksdb_transaction (rocksdb_store->tx_begin_write ({}, { vban::tables::final_votes }));
				rocksdb_store->final_vote_put (rocksdb_transaction, i->first, i->second);
			}
		});

		auto lmdb_transaction (store.tx_begin_read ());
		auto version = store.version_get (lmdb_transaction);
		auto rocksdb_transaction (rocksdb_store->tx_begin_write ());
		rocksdb_store->version_put (rocksdb_transaction, version);

		for (auto i (store.online_weight_begin (lmdb_transaction)), n (store.online_weight_end ()); i != n; ++i)
		{
			rocksdb_store->online_weight_put (rocksdb_transaction, i->first, i->second);
		}

		for (auto i (store.peers_begin (lmdb_transaction)), n (store.peers_end ()); i != n; ++i)
		{
			rocksdb_store->peer_put (rocksdb_transaction, i->first);
		}

		// Compare counts
		error |= store.unchecked_count (lmdb_transaction) != rocksdb_store->unchecked_count (rocksdb_transaction);
		error |= store.peer_count (lmdb_transaction) != rocksdb_store->peer_count (rocksdb_transaction);
		error |= store.pruned_count (lmdb_transaction) != rocksdb_store->pruned_count (rocksdb_transaction);
		error |= store.final_vote_count (lmdb_transaction) != rocksdb_store->final_vote_count (rocksdb_transaction);
		error |= store.online_weight_count (lmdb_transaction) != rocksdb_store->online_weight_count (rocksdb_transaction);
		error |= store.version_get (lmdb_transaction) != rocksdb_store->version_get (rocksdb_transaction);

		// For large tables a random key is used instead and makes sure it exists
		auto random_block (store.block_random (lmdb_transaction));
		error |= rocksdb_store->block_get (rocksdb_transaction, random_block->hash ()) == nullptr;

		auto account = random_block->account ().is_zero () ? random_block->sideband ().account : random_block->account ();
		vban::account_info account_info;
		error |= rocksdb_store->account_get (rocksdb_transaction, account, account_info);

		// If confirmation height exists in the lmdb ledger for this account it should exist in the rocksdb ledger
		vban::confirmation_height_info confirmation_height_info;
		if (!store.confirmation_height_get (lmdb_transaction, account, confirmation_height_info))
		{
			error |= rocksdb_store->confirmation_height_get (rocksdb_transaction, account, confirmation_height_info);
		}
	}
	else
	{
		error = true;
	}
	return error;
}

vban::uncemented_info::uncemented_info (vban::block_hash const & cemented_frontier, vban::block_hash const & frontier, vban::account const & account) :
	cemented_frontier (cemented_frontier), frontier (frontier), account (account)
{
}

std::unique_ptr<vban::container_info_component> vban::collect_container_info (ledger & ledger, std::string const & name)
{
	auto count = ledger.bootstrap_weights_size.load ();
	auto sizeof_element = sizeof (decltype (ledger.bootstrap_weights)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "bootstrap_weights", count, sizeof_element }));
	composite->add_component (collect_container_info (ledger.cache.rep_weights, "rep_weights"));
	return composite;
}
