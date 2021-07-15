#pragma once

#include <vban/lib/config.hpp>
#include <vban/lib/rep_weights.hpp>
#include <vban/lib/threading.hpp>
#include <vban/lib/timer.hpp>
#include <vban/secure/blockstore.hpp>
#include <vban/secure/buffer.hpp>

#include <crypto/cryptopp/words.h>

#include <thread>

#define release_assert_success(status)                 \
	if (!success (status))                             \
	{                                                  \
		release_assert (false, error_string (status)); \
	}
namespace
{
template <typename T>
void parallel_traversal (std::function<void (T const &, T const &, bool const)> const & action);
}

namespace vban
{
template <typename Val, typename Derived_Store>
class block_predecessor_set;

/** This base class implements the block_store interface functions which have DB agnostic functionality */
template <typename Val, typename Derived_Store>
class block_store_partial : public block_store
{
public:
	using block_store::block_exists;
	using block_store::unchecked_put;

	friend class vban::block_predecessor_set<Val, Derived_Store>;

	/**
	 * If using a different store version than the latest then you may need
	 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
	 */
	void initialize (vban::write_transaction const & transaction_a, vban::genesis const & genesis_a, vban::ledger_cache & ledger_cache_a) override
	{
		auto hash_l (genesis_a.hash ());
		debug_assert (accounts_begin (transaction_a) == accounts_end ());
		genesis_a.open->sideband_set (vban::block_sideband (network_params.ledger.genesis_account, 0, network_params.ledger.genesis_amount, 1, vban::seconds_since_epoch (), vban::epoch::epoch_0, false, false, false, vban::epoch::epoch_0));
		block_put (transaction_a, hash_l, *genesis_a.open);
		++ledger_cache_a.block_count;
		confirmation_height_put (transaction_a, network_params.ledger.genesis_account, vban::confirmation_height_info{ 1, genesis_a.hash () });
		++ledger_cache_a.cemented_count;
		ledger_cache_a.final_votes_confirmation_canary = (network_params.ledger.final_votes_canary_account == network_params.ledger.genesis_account && 1 >= network_params.ledger.final_votes_canary_height);
		account_put (transaction_a, network_params.ledger.genesis_account, { hash_l, network_params.ledger.genesis_account, genesis_a.open->hash (), vban::uint256_t ("50000000000000000000000000000000000000"), vban::seconds_since_epoch (), 1, vban::epoch::epoch_0 });
		++ledger_cache_a.account_count;
		ledger_cache_a.rep_weights.representation_put (network_params.ledger.genesis_account, vban::uint256_t ("50000000000000000000000000000000000000"));
		frontier_put (transaction_a, hash_l, network_params.ledger.genesis_account);
	}

	void block_put (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a, vban::block const & block_a) override
	{
		debug_assert (block_a.sideband ().successor.is_zero () || block_exists (transaction_a, block_a.sideband ().successor));
		std::vector<uint8_t> vector;
		{
			vban::vectorstream stream (vector);
			vban::serialize_block (stream, block_a);
			block_a.sideband ().serialize (stream, block_a.type ());
		}
		block_raw_put (transaction_a, vector, hash_a);
		vban::block_predecessor_set<Val, Derived_Store> predecessor (transaction_a, *this);
		block_a.visit (predecessor);
		debug_assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
	}

	// Converts a block hash to a block height
	uint64_t block_account_height (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		auto block = block_get (transaction_a, hash_a);
		return block->sideband ().height;
	}

	vban::uint256_t block_balance (vban::transaction const & transaction_a, vban::block_hash const & hash_a) override
	{
		auto block (block_get (transaction_a, hash_a));
		release_assert (block);
		vban::uint256_t result (block_balance_calculated (block));
		return result;
	}

	std::shared_ptr<vban::block> block_get (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		std::shared_ptr<vban::block> result;
		if (value.size () != 0)
		{
			vban::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			vban::block_type type;
			auto error (try_read (stream, type));
			release_assert (!error);
			result = vban::deserialize_block (stream, type);
			release_assert (result != nullptr);
			vban::block_sideband sideband;
			error = (sideband.deserialize (stream, type));
			release_assert (!error);
			result->sideband_set (sideband);
		}
		return result;
	}

	bool block_exists (vban::transaction const & transaction_a, vban::block_hash const & hash_a) override
	{
		auto junk = block_raw_get (transaction_a, hash_a);
		return junk.size () != 0;
	}

	std::shared_ptr<vban::block> block_get_no_sideband (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		std::shared_ptr<vban::block> result;
		if (value.size () != 0)
		{
			vban::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = vban::deserialize_block (stream);
			debug_assert (result != nullptr);
		}
		return result;
	}

	bool root_exists (vban::transaction const & transaction_a, vban::root const & root_a) override
	{
		return block_exists (transaction_a, root_a.as_block_hash ()) || account_exists (transaction_a, root_a.as_account ());
	}

	vban::account block_account (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		auto block (block_get (transaction_a, hash_a));
		debug_assert (block != nullptr);
		return block_account_calculated (*block);
	}

	vban::account block_account_calculated (vban::block const & block_a) const override
	{
		debug_assert (block_a.has_sideband ());
		vban::account result (block_a.account ());
		if (result.is_zero ())
		{
			result = block_a.sideband ().account;
		}
		debug_assert (!result.is_zero ());
		return result;
	}

	vban::uint256_t block_balance_calculated (std::shared_ptr<vban::block> const & block_a) const override
	{
		vban::uint256_t result;
		switch (block_a->type ())
		{
			case vban::block_type::open:
			case vban::block_type::receive:
			case vban::block_type::change:
				result = block_a->sideband ().balance.number ();
				break;
			case vban::block_type::send:
				result = boost::polymorphic_downcast<vban::send_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case vban::block_type::state:
				result = boost::polymorphic_downcast<vban::state_block *> (block_a.get ())->hashables.balance.number ();
				break;
			case vban::block_type::invalid:
			case vban::block_type::not_a_block:
				release_assert (false);
				break;
		}
		return result;
	}

	vban::block_hash block_successor (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		vban::block_hash result;
		if (value.size () != 0)
		{
			debug_assert (value.size () >= result.bytes.size ());
			auto type = block_type_from_raw (value.data ());
			vban::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()) + block_successor_offset (transaction_a, value.size (), type), result.bytes.size ());
			auto error (vban::try_read (stream, result.bytes));
			(void)error;
			debug_assert (!error);
		}
		else
		{
			result.clear ();
		}
		return result;
	}

	void block_successor_clear (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a) override
	{
		auto value (block_raw_get (transaction_a, hash_a));
		debug_assert (value.size () != 0);
		auto type = block_type_from_raw (value.data ());
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::fill_n (data.begin () + block_successor_offset (transaction_a, value.size (), type), sizeof (vban::block_hash), uint8_t{ 0 });
		block_raw_put (transaction_a, data, hash_a);
	}

	vban::store_iterator<vban::unchecked_key, vban::unchecked_info> unchecked_end () const override
	{
		return vban::store_iterator<vban::unchecked_key, vban::unchecked_info> (nullptr);
	}

	vban::store_iterator<vban::endpoint_key, vban::no_value> peers_end () const override
	{
		return vban::store_iterator<vban::endpoint_key, vban::no_value> (nullptr);
	}

	vban::store_iterator<vban::pending_key, vban::pending_info> pending_end () const override
	{
		return vban::store_iterator<vban::pending_key, vban::pending_info> (nullptr);
	}

	vban::store_iterator<uint64_t, vban::amount> online_weight_end () const override
	{
		return vban::store_iterator<uint64_t, vban::amount> (nullptr);
	}

	vban::store_iterator<vban::account, vban::account_info> accounts_end () const override
	{
		return vban::store_iterator<vban::account, vban::account_info> (nullptr);
	}

	vban::store_iterator<vban::block_hash, vban::block_w_sideband> blocks_end () const override
	{
		return vban::store_iterator<vban::block_hash, vban::block_w_sideband> (nullptr);
	}

	vban::store_iterator<vban::account, vban::confirmation_height_info> confirmation_height_end () const override
	{
		return vban::store_iterator<vban::account, vban::confirmation_height_info> (nullptr);
	}

	vban::store_iterator<vban::block_hash, std::nullptr_t> pruned_end () const override
	{
		return vban::store_iterator<vban::block_hash, std::nullptr_t> (nullptr);
	}

	vban::store_iterator<vban::qualified_root, vban::block_hash> final_vote_end () const override
	{
		return vban::store_iterator<vban::qualified_root, vban::block_hash> (nullptr);
	}

	vban::store_iterator<vban::block_hash, vban::account> frontiers_end () const override
	{
		return vban::store_iterator<vban::block_hash, vban::account> (nullptr);
	}

	int version_get (vban::transaction const & transaction_a) const override
	{
		vban::uint256_union version_key (1);
		vban::db_val<Val> data;
		auto status = get (transaction_a, tables::meta, vban::db_val<Val> (version_key), data);
		int result (minimum_version);
		if (success (status))
		{
			vban::uint256_union version_value (data);
			debug_assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
			result = version_value.number ().convert_to<int> ();
		}
		return result;
	}

	void block_del (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a) override
	{
		auto status = del (transaction_a, tables::blocks, hash_a);
		release_assert_success (status);
	}

	vban::epoch block_version (vban::transaction const & transaction_a, vban::block_hash const & hash_a) override
	{
		auto block = block_get (transaction_a, hash_a);
		if (block && block->type () == vban::block_type::state)
		{
			return block->sideband ().details.epoch;
		}

		return vban::epoch::epoch_0;
	}

	void block_raw_put (vban::write_transaction const & transaction_a, std::vector<uint8_t> const & data, vban::block_hash const & hash_a) override
	{
		vban::db_val<Val> value{ data.size (), (void *)data.data () };
		auto status = put (transaction_a, tables::blocks, hash_a, value);
		release_assert_success (status);
	}

	void pending_put (vban::write_transaction const & transaction_a, vban::pending_key const & key_a, vban::pending_info const & pending_info_a) override
	{
		vban::db_val<Val> pending (pending_info_a);
		auto status = put (transaction_a, tables::pending, key_a, pending);
		release_assert_success (status);
	}

	void pending_del (vban::write_transaction const & transaction_a, vban::pending_key const & key_a) override
	{
		auto status = del (transaction_a, tables::pending, key_a);
		release_assert_success (status);
	}

	bool pending_get (vban::transaction const & transaction_a, vban::pending_key const & key_a, vban::pending_info & pending_a) override
	{
		vban::db_val<Val> value;
		vban::db_val<Val> key (key_a);
		auto status1 = get (transaction_a, tables::pending, key, value);
		release_assert (success (status1) || not_found (status1));
		bool result (true);
		if (success (status1))
		{
			vban::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = pending_a.deserialize (stream);
		}
		return result;
	}

	bool pending_exists (vban::transaction const & transaction_a, vban::pending_key const & key_a) override
	{
		auto iterator (pending_begin (transaction_a, key_a));
		return iterator != pending_end () && vban::pending_key (iterator->first) == key_a;
	}

	bool pending_any (vban::transaction const & transaction_a, vban::account const & account_a) override
	{
		auto iterator (pending_begin (transaction_a, vban::pending_key (account_a, 0)));
		return iterator != pending_end () && vban::pending_key (iterator->first).account == account_a;
	}

	void frontier_put (vban::write_transaction const & transaction_a, vban::block_hash const & block_a, vban::account const & account_a) override
	{
		vban::db_val<Val> account (account_a);
		auto status (put (transaction_a, tables::frontiers, block_a, account));
		release_assert_success (status);
	}

	vban::account frontier_get (vban::transaction const & transaction_a, vban::block_hash const & block_a) const override
	{
		vban::db_val<Val> value;
		auto status (get (transaction_a, tables::frontiers, vban::db_val<Val> (block_a), value));
		release_assert (success (status) || not_found (status));
		vban::account result (0);
		if (success (status))
		{
			result = static_cast<vban::account> (value);
		}
		return result;
	}

	void frontier_del (vban::write_transaction const & transaction_a, vban::block_hash const & block_a) override
	{
		auto status (del (transaction_a, tables::frontiers, block_a));
		release_assert_success (status);
	}

	void unchecked_put (vban::write_transaction const & transaction_a, vban::unchecked_key const & key_a, vban::unchecked_info const & info_a) override
	{
		vban::db_val<Val> info (info_a);
		auto status (put (transaction_a, tables::unchecked, key_a, info));
		release_assert_success (status);
	}

	void unchecked_del (vban::write_transaction const & transaction_a, vban::unchecked_key const & key_a) override
	{
		auto status (del (transaction_a, tables::unchecked, key_a));
		release_assert_success (status);
	}

	bool unchecked_exists (vban::transaction const & transaction_a, vban::unchecked_key const & unchecked_key_a) override
	{
		vban::db_val<Val> value;
		auto status (get (transaction_a, tables::unchecked, vban::db_val<Val> (unchecked_key_a), value));
		release_assert (success (status) || not_found (status));
		return (success (status));
	}

	void unchecked_put (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a, std::shared_ptr<vban::block> const & block_a) override
	{
		vban::unchecked_key key (hash_a, block_a->hash ());
		vban::unchecked_info info (block_a, block_a->account (), vban::seconds_since_epoch (), vban::signature_verification::unknown);
		unchecked_put (transaction_a, key, info);
	}

	void unchecked_clear (vban::write_transaction const & transaction_a) override
	{
		auto status = drop (transaction_a, tables::unchecked);
		release_assert_success (status);
	}

	void account_put (vban::write_transaction const & transaction_a, vban::account const & account_a, vban::account_info const & info_a) override
	{
		// Check we are still in sync with other tables
		vban::db_val<Val> info (info_a);
		auto status = put (transaction_a, tables::accounts, account_a, info);
		release_assert_success (status);
	}

	void account_del (vban::write_transaction const & transaction_a, vban::account const & account_a) override
	{
		auto status = del (transaction_a, tables::accounts, account_a);
		release_assert_success (status);
	}

	bool account_get (vban::transaction const & transaction_a, vban::account const & account_a, vban::account_info & info_a) override
	{
		vban::db_val<Val> value;
		vban::db_val<Val> account (account_a);
		auto status1 (get (transaction_a, tables::accounts, account, value));
		release_assert (success (status1) || not_found (status1));
		bool result (true);
		if (success (status1))
		{
			vban::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = info_a.deserialize (stream);
		}
		return result;
	}

	bool account_exists (vban::transaction const & transaction_a, vban::account const & account_a) override
	{
		auto iterator (accounts_begin (transaction_a, account_a));
		return iterator != accounts_end () && vban::account (iterator->first) == account_a;
	}

	void online_weight_put (vban::write_transaction const & transaction_a, uint64_t time_a, vban::amount const & amount_a) override
	{
		vban::db_val<Val> value (amount_a);
		auto status (put (transaction_a, tables::online_weight, time_a, value));
		release_assert_success (status);
	}

	void online_weight_del (vban::write_transaction const & transaction_a, uint64_t time_a) override
	{
		auto status (del (transaction_a, tables::online_weight, time_a));
		release_assert_success (status);
	}

	size_t online_weight_count (vban::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::online_weight);
	}

	void online_weight_clear (vban::write_transaction const & transaction_a) override
	{
		auto status (drop (transaction_a, tables::online_weight));
		release_assert_success (status);
	}

	void pruned_put (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a) override
	{
		auto status = put_key (transaction_a, tables::pruned, hash_a);
		release_assert_success (status);
	}

	void pruned_del (vban::write_transaction const & transaction_a, vban::block_hash const & hash_a) override
	{
		auto status = del (transaction_a, tables::pruned, hash_a);
		release_assert_success (status);
	}

	bool pruned_exists (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		return exists (transaction_a, tables::pruned, vban::db_val<Val> (hash_a));
	}

	size_t pruned_count (vban::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::pruned);
	}

	void pruned_clear (vban::write_transaction const & transaction_a) override
	{
		auto status = drop (transaction_a, tables::pruned);
		release_assert_success (status);
	}

	void peer_put (vban::write_transaction const & transaction_a, vban::endpoint_key const & endpoint_a) override
	{
		auto status = put_key (transaction_a, tables::peers, endpoint_a);
		release_assert_success (status);
	}

	void peer_del (vban::write_transaction const & transaction_a, vban::endpoint_key const & endpoint_a) override
	{
		auto status (del (transaction_a, tables::peers, endpoint_a));
		release_assert_success (status);
	}

	bool peer_exists (vban::transaction const & transaction_a, vban::endpoint_key const & endpoint_a) const override
	{
		return exists (transaction_a, tables::peers, vban::db_val<Val> (endpoint_a));
	}

	size_t peer_count (vban::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::peers);
	}

	void peer_clear (vban::write_transaction const & transaction_a) override
	{
		auto status = drop (transaction_a, tables::peers);
		release_assert_success (status);
	}

	bool exists (vban::transaction const & transaction_a, tables table_a, vban::db_val<Val> const & key_a) const
	{
		return static_cast<const Derived_Store &> (*this).exists (transaction_a, table_a, key_a);
	}

	uint64_t block_count (vban::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::blocks);
	}

	size_t account_count (vban::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::accounts);
	}

	std::shared_ptr<vban::block> block_random (vban::transaction const & transaction_a) override
	{
		vban::block_hash hash;
		vban::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
		auto existing = make_iterator<vban::block_hash, std::shared_ptr<vban::block>> (transaction_a, tables::blocks, vban::db_val<Val> (hash));
		auto end (vban::store_iterator<vban::block_hash, std::shared_ptr<vban::block>> (nullptr));
		if (existing == end)
		{
			existing = make_iterator<vban::block_hash, std::shared_ptr<vban::block>> (transaction_a, tables::blocks);
		}
		debug_assert (existing != end);
		return existing->second;
	}

	vban::block_hash pruned_random (vban::transaction const & transaction_a) override
	{
		vban::block_hash random_hash;
		vban::random_pool::generate_block (random_hash.bytes.data (), random_hash.bytes.size ());
		auto existing = make_iterator<vban::block_hash, vban::db_val<Val>> (transaction_a, tables::pruned, vban::db_val<Val> (random_hash));
		auto end (vban::store_iterator<vban::block_hash, vban::db_val<Val>> (nullptr));
		if (existing == end)
		{
			existing = make_iterator<vban::block_hash, vban::db_val<Val>> (transaction_a, tables::pruned);
		}
		return existing != end ? existing->first : 0;
	}

	uint64_t confirmation_height_count (vban::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::confirmation_height);
	}

	void confirmation_height_put (vban::write_transaction const & transaction_a, vban::account const & account_a, vban::confirmation_height_info const & confirmation_height_info_a) override
	{
		vban::db_val<Val> confirmation_height_info (confirmation_height_info_a);
		auto status = put (transaction_a, tables::confirmation_height, account_a, confirmation_height_info);
		release_assert_success (status);
	}

	bool confirmation_height_get (vban::transaction const & transaction_a, vban::account const & account_a, vban::confirmation_height_info & confirmation_height_info_a) override
	{
		vban::db_val<Val> value;
		auto status = get (transaction_a, tables::confirmation_height, vban::db_val<Val> (account_a), value);
		release_assert (success (status) || not_found (status));
		bool result (true);
		if (success (status))
		{
			vban::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
			result = confirmation_height_info_a.deserialize (stream);
		}
		if (result)
		{
			confirmation_height_info_a.height = 0;
			confirmation_height_info_a.frontier = 0;
		}

		return result;
	}

	void confirmation_height_del (vban::write_transaction const & transaction_a, vban::account const & account_a) override
	{
		auto status (del (transaction_a, tables::confirmation_height, vban::db_val<Val> (account_a)));
		release_assert_success (status);
	}

	bool confirmation_height_exists (vban::transaction const & transaction_a, vban::account const & account_a) const override
	{
		return exists (transaction_a, tables::confirmation_height, vban::db_val<Val> (account_a));
	}

	bool final_vote_put (vban::write_transaction const & transaction_a, vban::qualified_root const & root_a, vban::block_hash const & hash_a) override
	{
		vban::db_val<Val> value;
		auto status = get (transaction_a, tables::final_votes, vban::db_val<Val> (root_a), value);
		release_assert (success (status) || not_found (status));
		bool result (true);
		if (success (status))
		{
			result = static_cast<vban::block_hash> (value) == hash_a;
		}
		else
		{
			status = put (transaction_a, tables::final_votes, root_a, hash_a);
			release_assert_success (status);
		}
		return result;
	}

	std::vector<vban::block_hash> final_vote_get (vban::transaction const & transaction_a, vban::root const & root_a) override
	{
		std::vector<vban::block_hash> result;
		vban::qualified_root key_start (root_a.raw, 0);
		for (auto i (final_vote_begin (transaction_a, key_start)), n (final_vote_end ()); i != n && vban::qualified_root (i->first).root () == root_a; ++i)
		{
			result.push_back (i->second);
		}
		return result;
	}

	size_t final_vote_count (vban::transaction const & transaction_a) const override
	{
		return count (transaction_a, tables::final_votes);
	}

	void final_vote_del (vban::write_transaction const & transaction_a, vban::root const & root_a) override
	{
		std::vector<vban::qualified_root> final_vote_qualified_roots;
		for (auto i (final_vote_begin (transaction_a, vban::qualified_root (root_a.raw, 0))), n (final_vote_end ()); i != n && vban::qualified_root (i->first).root () == root_a; ++i)
		{
			final_vote_qualified_roots.push_back (i->first);
		}

		for (auto & final_vote_qualified_root : final_vote_qualified_roots)
		{
			auto status (del (transaction_a, tables::final_votes, vban::db_val<Val> (final_vote_qualified_root)));
			release_assert_success (status);
		}
	}

	void final_vote_clear (vban::write_transaction const & transaction_a, vban::root const & root_a) override
	{
		final_vote_del (transaction_a, root_a);
	}

	void final_vote_clear (vban::write_transaction const & transaction_a) override
	{
		drop (transaction_a, vban::tables::final_votes);
	}

	void confirmation_height_clear (vban::write_transaction const & transaction_a, vban::account const & account_a) override
	{
		confirmation_height_del (transaction_a, account_a);
	}

	void confirmation_height_clear (vban::write_transaction const & transaction_a) override
	{
		drop (transaction_a, vban::tables::confirmation_height);
	}

	vban::store_iterator<vban::account, vban::account_info> accounts_begin (vban::transaction const & transaction_a, vban::account const & account_a) const override
	{
		return make_iterator<vban::account, vban::account_info> (transaction_a, tables::accounts, vban::db_val<Val> (account_a));
	}

	vban::store_iterator<vban::account, vban::account_info> accounts_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::account, vban::account_info> (transaction_a, tables::accounts);
	}

	vban::store_iterator<vban::block_hash, vban::block_w_sideband> blocks_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::block_hash, vban::block_w_sideband> (transaction_a, tables::blocks);
	}

	vban::store_iterator<vban::block_hash, vban::block_w_sideband> blocks_begin (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		return make_iterator<vban::block_hash, vban::block_w_sideband> (transaction_a, tables::blocks, vban::db_val<Val> (hash_a));
	}

	vban::store_iterator<vban::block_hash, vban::account> frontiers_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::block_hash, vban::account> (transaction_a, tables::frontiers);
	}

	vban::store_iterator<vban::block_hash, vban::account> frontiers_begin (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		return make_iterator<vban::block_hash, vban::account> (transaction_a, tables::frontiers, vban::db_val<Val> (hash_a));
	}

	vban::store_iterator<vban::pending_key, vban::pending_info> pending_begin (vban::transaction const & transaction_a, vban::pending_key const & key_a) const override
	{
		return make_iterator<vban::pending_key, vban::pending_info> (transaction_a, tables::pending, vban::db_val<Val> (key_a));
	}

	vban::store_iterator<vban::pending_key, vban::pending_info> pending_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::pending_key, vban::pending_info> (transaction_a, tables::pending);
	}

	vban::store_iterator<vban::unchecked_key, vban::unchecked_info> unchecked_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::unchecked_key, vban::unchecked_info> (transaction_a, tables::unchecked);
	}

	vban::store_iterator<vban::unchecked_key, vban::unchecked_info> unchecked_begin (vban::transaction const & transaction_a, vban::unchecked_key const & key_a) const override
	{
		return make_iterator<vban::unchecked_key, vban::unchecked_info> (transaction_a, tables::unchecked, vban::db_val<Val> (key_a));
	}

	vban::store_iterator<uint64_t, vban::amount> online_weight_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<uint64_t, vban::amount> (transaction_a, tables::online_weight);
	}

	vban::store_iterator<vban::endpoint_key, vban::no_value> peers_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::endpoint_key, vban::no_value> (transaction_a, tables::peers);
	}

	vban::store_iterator<vban::account, vban::confirmation_height_info> confirmation_height_begin (vban::transaction const & transaction_a, vban::account const & account_a) const override
	{
		return make_iterator<vban::account, vban::confirmation_height_info> (transaction_a, tables::confirmation_height, vban::db_val<Val> (account_a));
	}

	vban::store_iterator<vban::account, vban::confirmation_height_info> confirmation_height_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::account, vban::confirmation_height_info> (transaction_a, tables::confirmation_height);
	}

	vban::store_iterator<vban::block_hash, std::nullptr_t> pruned_begin (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const override
	{
		return make_iterator<vban::block_hash, std::nullptr_t> (transaction_a, tables::pruned, vban::db_val<Val> (hash_a));
	}

	vban::store_iterator<vban::block_hash, std::nullptr_t> pruned_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::block_hash, std::nullptr_t> (transaction_a, tables::pruned);
	}

	vban::store_iterator<vban::qualified_root, vban::block_hash> final_vote_begin (vban::transaction const & transaction_a, vban::qualified_root const & root_a) const override
	{
		return make_iterator<vban::qualified_root, vban::block_hash> (transaction_a, tables::final_votes, vban::db_val<Val> (root_a));
	}

	vban::store_iterator<vban::qualified_root, vban::block_hash> final_vote_begin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::qualified_root, vban::block_hash> (transaction_a, tables::final_votes);
	}

	vban::store_iterator<vban::account, vban::account_info> accounts_rbegin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<vban::account, vban::account_info> (transaction_a, tables::accounts, false);
	}

	vban::store_iterator<uint64_t, vban::amount> online_weight_rbegin (vban::transaction const & transaction_a) const override
	{
		return make_iterator<uint64_t, vban::amount> (transaction_a, tables::online_weight, false);
	}

	size_t unchecked_count (vban::transaction const & transaction_a) override
	{
		return count (transaction_a, tables::unchecked);
	}

	void accounts_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::account, vban::account_info>, vban::store_iterator<vban::account, vban::account_info>)> const & action_a) const override
	{
		parallel_traversal<vban::uint256_t> (
		[&action_a, this] (vban::uint256_t const & start, vban::uint256_t const & end, bool const is_last) {
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->accounts_begin (transaction, start), !is_last ? this->accounts_begin (transaction, end) : this->accounts_end ());
		});
	}

	void confirmation_height_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::account, vban::confirmation_height_info>, vban::store_iterator<vban::account, vban::confirmation_height_info>)> const & action_a) const override
	{
		parallel_traversal<vban::uint256_t> (
		[&action_a, this] (vban::uint256_t const & start, vban::uint256_t const & end, bool const is_last) {
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->confirmation_height_begin (transaction, start), !is_last ? this->confirmation_height_begin (transaction, end) : this->confirmation_height_end ());
		});
	}

	void pending_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::pending_key, vban::pending_info>, vban::store_iterator<vban::pending_key, vban::pending_info>)> const & action_a) const override
	{
		parallel_traversal<vban::uint512_t> (
		[&action_a, this] (vban::uint512_t const & start, vban::uint512_t const & end, bool const is_last) {
			vban::uint512_union union_start (start);
			vban::uint512_union union_end (end);
			vban::pending_key key_start (union_start.uint256s[0].number (), union_start.uint256s[1].number ());
			vban::pending_key key_end (union_end.uint256s[0].number (), union_end.uint256s[1].number ());
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->pending_begin (transaction, key_start), !is_last ? this->pending_begin (transaction, key_end) : this->pending_end ());
		});
	}

	void unchecked_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::unchecked_key, vban::unchecked_info>, vban::store_iterator<vban::unchecked_key, vban::unchecked_info>)> const & action_a) const override
	{
		parallel_traversal<vban::uint512_t> (
		[&action_a, this] (vban::uint512_t const & start, vban::uint512_t const & end, bool const is_last) {
			vban::unchecked_key key_start (start);
			vban::unchecked_key key_end (end);
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->unchecked_begin (transaction, key_start), !is_last ? this->unchecked_begin (transaction, key_end) : this->unchecked_end ());
		});
	}

	void blocks_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::block_hash, block_w_sideband>, vban::store_iterator<vban::block_hash, block_w_sideband>)> const & action_a) const override
	{
		parallel_traversal<vban::uint256_t> (
		[&action_a, this] (vban::uint256_t const & start, vban::uint256_t const & end, bool const is_last) {
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->blocks_begin (transaction, start), !is_last ? this->blocks_begin (transaction, end) : this->blocks_end ());
		});
	}

	void pruned_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::block_hash, std::nullptr_t>, vban::store_iterator<vban::block_hash, std::nullptr_t>)> const & action_a) const override
	{
		parallel_traversal<vban::uint256_t> (
		[&action_a, this] (vban::uint256_t const & start, vban::uint256_t const & end, bool const is_last) {
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->pruned_begin (transaction, start), !is_last ? this->pruned_begin (transaction, end) : this->pruned_end ());
		});
	}

	void frontiers_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::block_hash, vban::account>, vban::store_iterator<vban::block_hash, vban::account>)> const & action_a) const override
	{
		parallel_traversal<vban::uint256_t> (
		[&action_a, this] (vban::uint256_t const & start, vban::uint256_t const & end, bool const is_last) {
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->frontiers_begin (transaction, start), !is_last ? this->frontiers_begin (transaction, end) : this->frontiers_end ());
		});
	}

	void final_vote_for_each_par (std::function<void (vban::read_transaction const &, vban::store_iterator<vban::qualified_root, vban::block_hash>, vban::store_iterator<vban::qualified_root, vban::block_hash>)> const & action_a) const override
	{
		parallel_traversal<vban::uint512_t> (
		[&action_a, this] (vban::uint512_t const & start, vban::uint512_t const & end, bool const is_last) {
			auto transaction (this->tx_begin_read ());
			action_a (transaction, this->final_vote_begin (transaction, start), !is_last ? this->final_vote_begin (transaction, end) : this->final_vote_end ());
		});
	}

	int const minimum_version{ 14 };

protected:
	vban::network_params network_params;
	int const version{ 21 };

	template <typename Key, typename Value>
	vban::store_iterator<Key, Value> make_iterator (vban::transaction const & transaction_a, tables table_a, bool const direction_asc = true) const
	{
		return static_cast<Derived_Store const &> (*this).template make_iterator<Key, Value> (transaction_a, table_a, direction_asc);
	}

	template <typename Key, typename Value>
	vban::store_iterator<Key, Value> make_iterator (vban::transaction const & transaction_a, tables table_a, vban::db_val<Val> const & key) const
	{
		return static_cast<Derived_Store const &> (*this).template make_iterator<Key, Value> (transaction_a, table_a, key);
	}

	vban::db_val<Val> block_raw_get (vban::transaction const & transaction_a, vban::block_hash const & hash_a) const
	{
		vban::db_val<Val> result;
		auto status = get (transaction_a, tables::blocks, hash_a, result);
		release_assert (success (status) || not_found (status));
		return result;
	}

	size_t block_successor_offset (vban::transaction const & transaction_a, size_t entry_size_a, vban::block_type type_a) const
	{
		return entry_size_a - vban::block_sideband::size (type_a);
	}

	static vban::block_type block_type_from_raw (void * data_a)
	{
		// The block type is the first byte
		return static_cast<vban::block_type> ((reinterpret_cast<uint8_t const *> (data_a))[0]);
	}

	uint64_t count (vban::transaction const & transaction_a, std::initializer_list<tables> dbs_a) const
	{
		uint64_t total_count = 0;
		for (auto db : dbs_a)
		{
			total_count += count (transaction_a, db);
		}
		return total_count;
	}

	int get (vban::transaction const & transaction_a, tables table_a, vban::db_val<Val> const & key_a, vban::db_val<Val> & value_a) const
	{
		return static_cast<Derived_Store const &> (*this).get (transaction_a, table_a, key_a, value_a);
	}

	int put (vban::write_transaction const & transaction_a, tables table_a, vban::db_val<Val> const & key_a, vban::db_val<Val> const & value_a)
	{
		return static_cast<Derived_Store &> (*this).put (transaction_a, table_a, key_a, value_a);
	}

	// Put only key without value
	int put_key (vban::write_transaction const & transaction_a, tables table_a, vban::db_val<Val> const & key_a)
	{
		return put (transaction_a, table_a, key_a, vban::db_val<Val>{ nullptr });
	}

	int del (vban::write_transaction const & transaction_a, tables table_a, vban::db_val<Val> const & key_a)
	{
		return static_cast<Derived_Store &> (*this).del (transaction_a, table_a, key_a);
	}

	virtual uint64_t count (vban::transaction const & transaction_a, tables table_a) const = 0;
	virtual int drop (vban::write_transaction const & transaction_a, tables table_a) = 0;
	virtual bool not_found (int status) const = 0;
	virtual bool success (int status) const = 0;
	virtual int status_code_not_found () const = 0;
	virtual std::string error_string (int status) const = 0;
};

/**
 * Fill in our predecessors
 */
template <typename Val, typename Derived_Store>
class block_predecessor_set : public vban::block_visitor
{
public:
	block_predecessor_set (vban::write_transaction const & transaction_a, vban::block_store_partial<Val, Derived_Store> & store_a) :
		transaction (transaction_a),
		store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (vban::block const & block_a)
	{
		auto hash (block_a.hash ());
		auto value (store.block_raw_get (transaction, block_a.previous ()));
		debug_assert (value.size () != 0);
		auto type = store.block_type_from_raw (value.data ());
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.data ()), static_cast<uint8_t *> (value.data ()) + value.size ());
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + store.block_successor_offset (transaction, value.size (), type));
		store.block_raw_put (transaction, data, block_a.previous ());
	}
	void send_block (vban::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (vban::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (vban::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (vban::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (vban::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	vban::write_transaction const & transaction;
	vban::block_store_partial<Val, Derived_Store> & store;
};
}

namespace
{
template <typename T>
void parallel_traversal (std::function<void (T const &, T const &, bool const)> const & action)
{
	// Between 10 and 40 threads, scales well even in low power systems as long as actions are I/O bound
	unsigned const thread_count = std::max (10u, std::min (40u, 10 * std::thread::hardware_concurrency ()));
	T const value_max{ std::numeric_limits<T>::max () };
	T const split = value_max / thread_count;
	std::vector<std::thread> threads;
	threads.reserve (thread_count);
	for (unsigned thread (0); thread < thread_count; ++thread)
	{
		T const start = thread * split;
		T const end = (thread + 1) * split;
		bool const is_last = thread == thread_count - 1;

		threads.emplace_back ([&action, start, end, is_last] {
			vban::thread_role::set (vban::thread_role::name::db_parallel_traversal);
			action (start, end, is_last);
		});
	}
	for (auto & thread : threads)
	{
		thread.join ();
	}
}
}
