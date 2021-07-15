#pragma once

#include <vban/lib/rep_weights.hpp>
#include <vban/lib/timer.hpp>
#include <vban/secure/common.hpp>

#include <map>

namespace vban
{
class block_store;
class stat;
class write_transaction;

using tally_t = std::map<vban::uint256_t, std::shared_ptr<vban::block>, std::greater<vban::uint256_t>>;

class uncemented_info
{
public:
	uncemented_info (vban::block_hash const & cemented_frontier, vban::block_hash const & frontier, vban::account const & account);
	vban::block_hash cemented_frontier;
	vban::block_hash frontier;
	vban::account account;
};

class ledger final
{
public:
	ledger (vban::block_store &, vban::stat &, vban::generate_cache const & = vban::generate_cache ());
	vban::account account (vban::transaction const &, vban::block_hash const &) const;
	vban::account account_safe (vban::transaction const &, vban::block_hash const &, bool &) const;
	vban::uint256_t amount (vban::transaction const &, vban::account const &);
	vban::uint256_t amount (vban::transaction const &, vban::block_hash const &);
	/** Safe for previous block, but block hash_a must exist */
	vban::uint256_t amount_safe (vban::transaction const &, vban::block_hash const & hash_a, bool &) const;
	vban::uint256_t balance (vban::transaction const &, vban::block_hash const &) const;
	vban::uint256_t balance_safe (vban::transaction const &, vban::block_hash const &, bool &) const;
	vban::uint256_t account_balance (vban::transaction const &, vban::account const &, bool = false);
	vban::uint256_t account_pending (vban::transaction const &, vban::account const &, bool = false);
	vban::uint256_t weight (vban::account const &);
	std::shared_ptr<vban::block> successor (vban::transaction const &, vban::qualified_root const &);
	std::shared_ptr<vban::block> forked_block (vban::transaction const &, vban::block const &);
	bool block_confirmed (vban::transaction const &, vban::block_hash const &) const;
	vban::block_hash latest (vban::transaction const &, vban::account const &);
	vban::root latest_root (vban::transaction const &, vban::account const &);
	vban::block_hash representative (vban::transaction const &, vban::block_hash const &);
	vban::block_hash representative_calculated (vban::transaction const &, vban::block_hash const &);
	bool block_or_pruned_exists (vban::block_hash const &) const;
	bool block_or_pruned_exists (vban::transaction const &, vban::block_hash const &) const;
	std::string block_text (char const *);
	std::string block_text (vban::block_hash const &);
	bool is_send (vban::transaction const &, vban::state_block const &) const;
	vban::account const & block_destination (vban::transaction const &, vban::block const &);
	vban::block_hash block_source (vban::transaction const &, vban::block const &);
	std::pair<vban::block_hash, vban::block_hash> hash_root_random (vban::transaction const &) const;
	vban::process_return process (vban::write_transaction const &, vban::block &, vban::signature_verification = vban::signature_verification::unknown);
	bool rollback (vban::write_transaction const &, vban::block_hash const &, std::vector<std::shared_ptr<vban::block>> &);
	bool rollback (vban::write_transaction const &, vban::block_hash const &);
	void update_account (vban::write_transaction const &, vban::account const &, vban::account_info const &, vban::account_info const &);
	uint64_t pruning_action (vban::write_transaction &, vban::block_hash const &, uint64_t const);
	void dump_account_chain (vban::account const &, std::ostream & = std::cout);
	bool could_fit (vban::transaction const &, vban::block const &) const;
	bool dependents_confirmed (vban::transaction const &, vban::block const &) const;
	bool is_epoch_link (vban::link const &) const;
	std::array<vban::block_hash, 2> dependent_blocks (vban::transaction const &, vban::block const &) const;
	vban::account const & epoch_signer (vban::link const &) const;
	vban::link const & epoch_link (vban::epoch) const;
	std::multimap<uint64_t, uncemented_info, std::greater<>> unconfirmed_frontiers () const;
	bool migrate_lmdb_to_rocksdb (boost::filesystem::path const &) const;
	static vban::uint256_t const unit;
	vban::network_params network_params;
	vban::block_store & store;
	vban::ledger_cache cache;
	vban::stat & stats;
	std::unordered_map<vban::account, vban::uint256_t> bootstrap_weights;
	std::atomic<size_t> bootstrap_weights_size{ 0 };
	uint64_t bootstrap_weight_max_blocks{ 1 };
	std::atomic<bool> check_bootstrap_weights;
	bool pruning{ false };

private:
	void initialize (vban::generate_cache const &);
};

std::unique_ptr<container_info_component> collect_container_info (ledger & ledger, std::string const & name);
}
