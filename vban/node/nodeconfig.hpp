#pragma once

#include <vban/lib/config.hpp>
#include <vban/lib/diagnosticsconfig.hpp>
#include <vban/lib/errors.hpp>
#include <vban/lib/jsonconfig.hpp>
#include <vban/lib/lmdbconfig.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/lib/rocksdbconfig.hpp>
#include <vban/lib/stats.hpp>
#include <vban/node/ipc/ipc_config.hpp>
#include <vban/node/logging.hpp>
#include <vban/node/websocketconfig.hpp>
#include <vban/secure/common.hpp>

#include <chrono>
#include <vector>

namespace vban
{
class tomlconfig;

enum class frontiers_confirmation_mode : uint8_t
{
	always, // Always confirm frontiers
	automatic, // Always mode if node contains representative with at least 50% of principal weight, less frequest requests if not
	disabled, // Do not confirm frontiers
	invalid
};

/**
 * Node configuration
 */
class node_config
{
public:
	node_config ();
	node_config (uint16_t, vban::logging const &);
	vban::error serialize_json (vban::jsonconfig &) const;
	vban::error deserialize_json (bool &, vban::jsonconfig &);
	vban::error serialize_toml (vban::tomlconfig &) const;
	vban::error deserialize_toml (vban::tomlconfig &);
	bool upgrade_json (unsigned, vban::jsonconfig &);
	vban::account random_representative () const;
	vban::network_params network_params;
	uint16_t peering_port{ 0 };
	vban::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::pair<std::string, uint16_t>> secondary_work_peers{ { "127.0.0.1", 8076 } }; /* Default of nano-pow-server */
	std::vector<std::string> preconfigured_peers;
	std::vector<vban::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator{ 1 };
	vban::amount receive_minimum{ vban::xrb_ratio };
	vban::amount vote_minimum{ vban::Gxrb_ratio };
	std::chrono::milliseconds vote_generator_delay{ std::chrono::milliseconds (100) };
	unsigned vote_generator_threshold{ 3 };
	vban::amount online_weight_minimum{ 60000 * vban::Gxrb_ratio };
	unsigned election_hint_weight_percent{ 10 };
	unsigned password_fanout{ 1024 };
	unsigned io_threads{ std::max<unsigned> (4, std::thread::hardware_concurrency ()) };
	unsigned network_threads{ std::max<unsigned> (4, std::thread::hardware_concurrency ()) };
	unsigned work_threads{ std::max<unsigned> (4, std::thread::hardware_concurrency ()) };
	/* Use half available threads on the system for signature checking. The calling thread does checks as well, so these are extra worker threads */
	unsigned signature_checker_threads{ std::thread::hardware_concurrency () / 2 };
	bool enable_voting{ false };
	unsigned bootstrap_connections{ 4 };
	unsigned bootstrap_connections_max{ 64 };
	unsigned bootstrap_initiator_threads{ 1 };
	uint32_t bootstrap_frontier_request_count{ 1024 * 1024 };
	vban::websocket::config websocket_config;
	vban::diagnostics_config diagnostics_config;
	size_t confirmation_history_size{ 2048 };
	std::string callback_address;
	uint16_t callback_port{ 0 };
	std::string callback_target;
	[[deprecated]] int deprecated_lmdb_max_dbs{ 128 };
	bool allow_local_peers{ !(network_params.network.is_live_network () || network_params.network.is_test_network ()) }; // disable by default for live network
	vban::stat_config stat_config;
	vban::ipc::ipc_config ipc_config;
	std::string external_address;
	uint16_t external_port{ 0 };
	std::chrono::milliseconds block_processor_batch_max_time{ network_params.network.is_dev_network () ? std::chrono::milliseconds (500) : std::chrono::milliseconds (5000) };
	std::chrono::seconds unchecked_cutoff_time{ std::chrono::seconds (4 * 60 * 60) }; // 4 hours
	/** Timeout for initiated async operations */
	std::chrono::seconds tcp_io_timeout{ (network_params.network.is_dev_network () && !is_sanitizer_build) ? std::chrono::seconds (5) : std::chrono::seconds (15) };
	std::chrono::nanoseconds pow_sleep_interval{ 0 };
	size_t active_elections_size{ 5000 };
	/** Default maximum incoming TCP connections, including realtime network & bootstrap */
	unsigned tcp_incoming_connections_max{ 2048 };
	bool use_memory_pools{ true };
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	/** Default outbound traffic shaping is 10MB/s */
	size_t bandwidth_limit{ 10 * 1024 * 1024 };
	/** By default, allow bursts of 15MB/s (not sustainable) */
	double bandwidth_limit_burst_ratio{ 3. };
	std::chrono::milliseconds conf_height_processor_batch_min_time{ 50 };
	bool backup_before_upgrade{ false };
	double max_work_generate_multiplier{ 64. };
	uint32_t max_queued_requests{ 512 };
	/** Maximum amount of confirmation requests (batches) to be sent to each channel */
	uint32_t confirm_req_batches_max{ network_params.network.is_dev_network () ? 1u : 2u };
	std::chrono::seconds max_pruning_age{ !network_params.network.is_beta_network () ? std::chrono::seconds (24 * 60 * 60) : std::chrono::seconds (5 * 60) }; // 1 day; 5 minutes for beta network
	uint64_t max_pruning_depth{ 0 };
	vban::rocksdb_config rocksdb_config;
	vban::lmdb_config lmdb_config;
	vban::frontiers_confirmation_mode frontiers_confirmation{ vban::frontiers_confirmation_mode::automatic };
	std::string serialize_frontiers_confirmation (vban::frontiers_confirmation_mode) const;
	vban::frontiers_confirmation_mode deserialize_frontiers_confirmation (std::string const &);
	/** Entry is ignored if it cannot be parsed as a valid address:port */
	void deserialize_address (std::string const &, std::vector<std::pair<std::string, uint16_t>> &) const;

	static unsigned json_version ()
	{
		return 18;
	}
};

class node_flags final
{
public:
	std::vector<std::string> config_overrides;
	std::vector<std::string> rpc_config_overrides;
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_bootstrap_bulk_pull_server{ false };
	bool disable_bootstrap_bulk_push_client{ false };
	bool disable_ongoing_bootstrap{ false }; // For testing only
	bool disable_rep_crawler{ false };
	bool disable_request_loop{ false }; // For testing only
	bool disable_tcp_realtime{ false };
	bool disable_udp{ true };
	bool disable_unchecked_cleanup{ false };
	bool disable_unchecked_drop{ true };
	bool disable_providing_telemetry_metrics{ false };
	bool disable_ongoing_telemetry_requests{ false };
	bool disable_initial_telemetry_requests{ false };
	bool disable_block_processor_unchecked_deletion{ false };
	bool disable_block_processor_republishing{ false };
	bool allow_bootstrap_peers_duplicates{ false };
	bool disable_max_peers_per_ip{ false }; // For testing only
	bool force_use_write_database_queue{ false }; // For testing only. RocksDB does not use the database queue, but some tests rely on it being used.
	bool disable_search_pending{ false }; // For testing only
	bool enable_pruning{ false };
	bool fast_bootstrap{ false };
	bool read_only{ false };
	vban::confirmation_height_mode confirmation_height_processor_mode{ vban::confirmation_height_mode::automatic };
	vban::generate_cache generate_cache;
	bool inactive_node{ false };
	size_t block_processor_batch_size{ 0 };
	size_t block_processor_full_size{ 65536 };
	size_t block_processor_verification_size{ 0 };
	size_t inactive_votes_cache_size{ 16 * 1024 };
	size_t vote_processor_capacity{ 144 * 1024 };
	size_t bootstrap_interval{ 0 }; // For testing only
};
}
