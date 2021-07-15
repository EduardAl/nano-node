#pragma once

#include <vban/lib/errors.hpp>
#include <vban/node/node.hpp>

#include <chrono>

namespace vban
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system final
{
public:
	system ();
	system (uint16_t, vban::transport::transport_type = vban::transport::transport_type::tcp, vban::node_flags = vban::node_flags ());
	~system ();
	void generate_activity (vban::node &, std::vector<vban::account> &);
	void generate_mass_activity (uint32_t, vban::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	vban::account get_random_account (std::vector<vban::account> &);
	vban::uint256_t get_random_amount (vban::transaction const &, vban::node &, vban::account const &);
	void generate_rollback (vban::node &, std::vector<vban::account> &);
	void generate_change_known (vban::node &, std::vector<vban::account> &);
	void generate_change_unknown (vban::node &, std::vector<vban::account> &);
	void generate_receive (vban::node &);
	void generate_send_new (vban::node &, std::vector<vban::account> &);
	void generate_send_existing (vban::node &, std::vector<vban::account> &);
	std::unique_ptr<vban::state_block> upgrade_genesis_epoch (vban::node &, vban::epoch const);
	std::shared_ptr<vban::wallet> wallet (size_t);
	vban::account account (vban::transaction const &, size_t);
	/** Generate work with difficulty between \p min_difficulty_a (inclusive) and \p max_difficulty_a (exclusive) */
	uint64_t work_generate_limited (vban::block_hash const & root_a, uint64_t min_difficulty_a, uint64_t max_difficulty_a);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or vban::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	std::error_code poll_until_true (std::chrono::nanoseconds deadline, std::function<bool ()>);
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::nano> & delta);
	std::shared_ptr<vban::node> add_node (vban::node_flags = vban::node_flags (), vban::transport::transport_type = vban::transport::transport_type::tcp);
	std::shared_ptr<vban::node> add_node (vban::node_config const &, vban::node_flags = vban::node_flags (), vban::transport::transport_type = vban::transport::transport_type::tcp);
	boost::asio::io_context io_ctx;
	std::vector<std::shared_ptr<vban::node>> nodes;
	vban::logging logging;
	vban::work_pool work{ std::max (std::thread::hardware_concurrency (), 1u) };
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
	unsigned node_sequence{ 0 };
};
std::unique_ptr<vban::state_block> upgrade_epoch (vban::work_pool &, vban::ledger &, vban::epoch);
void blocks_confirm (vban::node &, std::vector<std::shared_ptr<vban::block>> const &, bool const = false);
uint16_t get_available_port ();
void cleanup_dev_directories_on_exit ();
/** To use RocksDB in tests make sure the environment variable TEST_USE_ROCKSDB=1 is set */
bool using_rocksdb_in_tests ();
}
REGISTER_ERROR_CODES (vban, error_system);
