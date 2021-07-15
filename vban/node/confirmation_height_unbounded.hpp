#pragma once

#include <vban/lib/numbers.hpp>
#include <vban/lib/threading.hpp>
#include <vban/lib/timer.hpp>
#include <vban/secure/blockstore.hpp>

#include <chrono>
#include <unordered_map>

namespace vban
{
class ledger;
class read_transaction;
class logging;
class logger_mt;
class write_database_queue;
class write_guard;

class confirmation_height_unbounded final
{
public:
	confirmation_height_unbounded (vban::ledger &, vban::write_database_queue &, std::chrono::milliseconds, vban::logging const &, vban::logger_mt &, std::atomic<bool> &, uint64_t &, std::function<void (std::vector<std::shared_ptr<vban::block>> const &)> const &, std::function<void (vban::block_hash const &)> const &, std::function<uint64_t ()> const &);
	bool pending_empty () const;
	void clear_process_vars ();
	void process (std::shared_ptr<vban::block> original_block);
	void cement_blocks (vban::write_guard &);
	bool has_iterated_over_block (vban::block_hash const &) const;

private:
	class confirmed_iterated_pair
	{
	public:
		confirmed_iterated_pair (uint64_t confirmed_height_a, uint64_t iterated_height_a);
		uint64_t confirmed_height;
		uint64_t iterated_height;
	};

	class conf_height_details final
	{
	public:
		conf_height_details (vban::account const &, vban::block_hash const &, uint64_t, uint64_t, std::vector<vban::block_hash> const &);

		vban::account account;
		vban::block_hash hash;
		uint64_t height;
		uint64_t num_blocks_confirmed;
		std::vector<vban::block_hash> block_callback_data;
		std::vector<vban::block_hash> source_block_callback_data;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (std::shared_ptr<conf_height_details> const &, const vban::block_hash &);

		std::shared_ptr<conf_height_details> receive_details;
		vban::block_hash source_hash;
	};

	// All of the atomic variables here just track the size for use in collect_container_info.
	// This is so that no mutexes are needed during the algorithm itself, which would otherwise be needed
	// for the sake of a rarely used RPC call for debugging purposes. As such the sizes are not being acted
	// upon in any way (does not synchronize with any other data).
	// This allows the load and stores to use relaxed atomic memory ordering.
	std::unordered_map<account, confirmed_iterated_pair> confirmed_iterated_pairs;
	vban::relaxed_atomic_integral<uint64_t> confirmed_iterated_pairs_size{ 0 };
	std::shared_ptr<vban::block> get_block_and_sideband (vban::block_hash const &, vban::transaction const &);
	std::deque<conf_height_details> pending_writes;
	vban::relaxed_atomic_integral<uint64_t> pending_writes_size{ 0 };
	std::unordered_map<vban::block_hash, std::weak_ptr<conf_height_details>> implicit_receive_cemented_mapping;
	vban::relaxed_atomic_integral<uint64_t> implicit_receive_cemented_mapping_size{ 0 };

	mutable vban::mutex block_cache_mutex;
	std::unordered_map<vban::block_hash, std::shared_ptr<vban::block>> block_cache;
	uint64_t block_cache_size () const;

	vban::timer<std::chrono::milliseconds> timer;

	class preparation_data final
	{
	public:
		uint64_t block_height;
		uint64_t confirmation_height;
		uint64_t iterated_height;
		decltype (confirmed_iterated_pairs.begin ()) account_it;
		vban::account const & account;
		std::shared_ptr<conf_height_details> receive_details;
		bool already_traversed;
		vban::block_hash const & current;
		std::vector<vban::block_hash> const & block_callback_data;
		std::vector<vban::block_hash> const & orig_block_callback_data;
	};

	void collect_unconfirmed_receive_and_sources_for_account (uint64_t, uint64_t, std::shared_ptr<vban::block> const &, vban::block_hash const &, vban::account const &, vban::read_transaction const &, std::vector<receive_source_pair> &, std::vector<vban::block_hash> &, std::vector<vban::block_hash> &, std::shared_ptr<vban::block> original_block);
	void prepare_iterated_blocks_for_cementing (preparation_data &);

	vban::network_params network_params;
	vban::ledger & ledger;
	vban::write_database_queue & write_database_queue;
	std::chrono::milliseconds batch_separate_pending_min_time;
	vban::logger_mt & logger;
	std::atomic<bool> & stopped;
	uint64_t & batch_write_size;
	vban::logging const & logging;

	std::function<void (std::vector<std::shared_ptr<vban::block>> const &)> notify_observers_callback;
	std::function<void (vban::block_hash const &)> notify_block_already_cemented_observers_callback;
	std::function<uint64_t ()> awaiting_processing_size_callback;

	friend class confirmation_height_dynamic_algorithm_no_transition_while_pending_Test;
	friend std::unique_ptr<vban::container_info_component> collect_container_info (confirmation_height_unbounded &, std::string const & name_a);
};

std::unique_ptr<vban::container_info_component> collect_container_info (confirmation_height_unbounded &, std::string const & name_a);
}
