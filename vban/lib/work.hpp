#pragma once

#include <vban/lib/config.hpp>
#include <vban/lib/locks.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/lib/utility.hpp>

#include <boost/optional.hpp>
#include <boost/thread/thread.hpp>

#include <atomic>
#include <memory>

namespace vban
{
enum class work_version
{
	unspecified,
	work_1
};
std::string to_string (vban::work_version const version_a);

class block;
class block_details;
enum class block_type : uint8_t;
bool work_validate_entry (vban::block const &);
bool work_validate_entry (vban::work_version const, vban::root const &, uint64_t const);

uint64_t work_difficulty (vban::work_version const, vban::root const &, uint64_t const);

uint64_t work_threshold_base (vban::work_version const);
uint64_t work_threshold_entry (vban::work_version const, vban::block_type const);
// Ledger threshold
uint64_t work_threshold (vban::work_version const, vban::block_details const);

namespace work_v1
{
	uint64_t value (vban::root const & root_a, uint64_t work_a);
	uint64_t threshold_base ();
	uint64_t threshold_entry ();
	uint64_t threshold (vban::block_details const);
}

double normalized_multiplier (double const, uint64_t const);
double denormalized_multiplier (double const, uint64_t const);
class opencl_work;
class work_item final
{
public:
	work_item (vban::work_version const version_a, vban::root const & item_a, uint64_t difficulty_a, std::function<void (boost::optional<uint64_t> const &)> const & callback_a) :
		version (version_a), item (item_a), difficulty (difficulty_a), callback (callback_a)
	{
	}
	vban::work_version const version;
	vban::root const item;
	uint64_t const difficulty;
	std::function<void (boost::optional<uint64_t> const &)> const callback;
};
class work_pool final
{
public:
	work_pool (unsigned, std::chrono::nanoseconds = std::chrono::nanoseconds (0), std::function<boost::optional<uint64_t> (vban::work_version const, vban::root const &, uint64_t, std::atomic<int> &)> = nullptr);
	~work_pool ();
	void loop (uint64_t);
	void stop ();
	void cancel (vban::root const &);
	void generate (vban::work_version const, vban::root const &, uint64_t, std::function<void (boost::optional<uint64_t> const &)>);
	boost::optional<uint64_t> generate (vban::work_version const, vban::root const &, uint64_t);
	// For tests only
	boost::optional<uint64_t> generate (vban::root const &);
	boost::optional<uint64_t> generate (vban::root const &, uint64_t);
	size_t size ();
	vban::network_constants network_constants;
	std::atomic<int> ticket;
	bool done;
	std::vector<boost::thread> threads;
	std::list<vban::work_item> pending;
	vban::mutex mutex{ mutex_identifier (mutexes::work_pool) };
	vban::condition_variable producer_condition;
	std::chrono::nanoseconds pow_rate_limiter;
	std::function<boost::optional<uint64_t> (vban::work_version const, vban::root const &, uint64_t, std::atomic<int> &)> opencl;
	vban::observer_set<bool> work_observers;
};

std::unique_ptr<container_info_component> collect_container_info (work_pool & work_pool, std::string const & name);
}
