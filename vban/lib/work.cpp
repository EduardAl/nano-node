#include <vban/crypto_lib/random_pool.hpp>
#include <vban/lib/blocks.hpp>
#include <vban/lib/epoch.hpp>
#include <vban/lib/threading.hpp>
#include <vban/lib/work.hpp>
#include <vban/node/xorshift.hpp>

#include <future>

std::string vban::to_string (vban::work_version const version_a)
{
	std::string result ("invalid");
	switch (version_a)
	{
		case vban::work_version::work_1:
			result = "work_1";
			break;
		case vban::work_version::unspecified:
			result = "unspecified";
			break;
	}
	return result;
}

bool vban::work_validate_entry (vban::block const & block_a)
{
	return block_a.difficulty () < vban::work_threshold_entry (block_a.work_version (), block_a.type ());
}

bool vban::work_validate_entry (vban::work_version const version_a, vban::root const & root_a, uint64_t const work_a)
{
	return vban::work_difficulty (version_a, root_a, work_a) < vban::work_threshold_entry (version_a, vban::block_type::state);
}

uint64_t vban::work_difficulty (vban::work_version const version_a, vban::root const & root_a, uint64_t const work_a)
{
	uint64_t result{ 0 };
	switch (version_a)
	{
		case vban::work_version::work_1:
			result = vban::work_v1::value (root_a, work_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to work_difficulty");
	}
	return result;
}

uint64_t vban::work_threshold_base (vban::work_version const version_a)
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case vban::work_version::work_1:
			result = vban::work_v1::threshold_base ();
			break;
		default:
			debug_assert (false && "Invalid version specified to work_threshold_base");
	}
	return result;
}

uint64_t vban::work_threshold_entry (vban::work_version const version_a, vban::block_type const type_a)
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	if (type_a == vban::block_type::state)
	{
		switch (version_a)
		{
			case vban::work_version::work_1:
				result = vban::work_v1::threshold_entry ();
				break;
			default:
				debug_assert (false && "Invalid version specified to work_threshold_entry");
		}
	}
	else
	{
		static vban::network_constants network_constants;
		result = network_constants.publish_thresholds.epoch_1;
	}
	return result;
}

uint64_t vban::work_threshold (vban::work_version const version_a, vban::block_details const details_a)
{
	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (version_a)
	{
		case vban::work_version::work_1:
			result = vban::work_v1::threshold (details_a);
			break;
		default:
			debug_assert (false && "Invalid version specified to ledger work_threshold");
	}
	return result;
}

uint64_t vban::work_v1::threshold_base ()
{
	static vban::network_constants network_constants;
	return network_constants.publish_thresholds.base;
}

uint64_t vban::work_v1::threshold_entry ()
{
	static vban::network_constants network_constants;
	return network_constants.publish_thresholds.entry;
}

uint64_t vban::work_v1::threshold (vban::block_details const details_a)
{
	static_assert (vban::epoch::max == vban::epoch::epoch_2, "work_v1::threshold is ill-defined");
	static vban::network_constants network_constants;

	uint64_t result{ std::numeric_limits<uint64_t>::max () };
	switch (details_a.epoch)
	{
		case vban::epoch::epoch_2:
			result = (details_a.is_receive || details_a.is_epoch) ? network_constants.publish_thresholds.epoch_2_receive : network_constants.publish_thresholds.epoch_2;
			break;
		case vban::epoch::epoch_1:
		case vban::epoch::epoch_0:
			result = network_constants.publish_thresholds.epoch_1;
			break;
		default:
			debug_assert (false && "Invalid epoch specified to work_v1 ledger work_threshold");
	}
	return result;
}

#ifndef VBAN_FUZZER_TEST
uint64_t vban::work_v1::value (vban::root const & root_a, uint64_t work_a)
{
	uint64_t result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result));
	blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work_a), sizeof (work_a));
	blake2b_update (&hash, root_a.bytes.data (), root_a.bytes.size ());
	blake2b_final (&hash, reinterpret_cast<uint8_t *> (&result), sizeof (result));
	return result;
}
#else
uint64_t vban::work_v1::value (vban::root const & root_a, uint64_t work_a)
{
	static vban::network_constants network_constants;
	if (!network_constants.is_dev_network ())
	{
		debug_assert (false);
		std::exit (1);
	}
	return network_constants.publish_thresholds.base + 1;
}
#endif

double vban::normalized_multiplier (double const multiplier_a, uint64_t const threshold_a)
{
	static vban::network_constants network_constants;
	debug_assert (multiplier_a >= 1);
	auto multiplier (multiplier_a);
	/* Normalization rules
	ratio = multiplier of max work threshold (send epoch 2) from given threshold
	i.e. max = 0xfe00000000000000, given = 0xf000000000000000, ratio = 8.0
	normalized = (multiplier + (ratio - 1)) / ratio;
	Epoch 1
	multiplier	 | normalized
	1.0 		 | 1.0
	9.0 		 | 2.0
	25.0 		 | 4.0
	Epoch 2 (receive / epoch subtypes)
	multiplier	 | normalized
	1.0 		 | 1.0
	65.0 		 | 2.0
	241.0 		 | 4.0
	*/
	if (threshold_a == network_constants.publish_thresholds.epoch_1 || threshold_a == network_constants.publish_thresholds.epoch_2_receive)
	{
		auto ratio (vban::difficulty::to_multiplier (network_constants.publish_thresholds.epoch_2, threshold_a));
		debug_assert (ratio >= 1);
		multiplier = (multiplier + (ratio - 1.0)) / ratio;
		debug_assert (multiplier >= 1);
	}
	return multiplier;
}

double vban::denormalized_multiplier (double const multiplier_a, uint64_t const threshold_a)
{
	static vban::network_constants network_constants;
	debug_assert (multiplier_a >= 1);
	auto multiplier (multiplier_a);
	if (threshold_a == network_constants.publish_thresholds.epoch_1 || threshold_a == network_constants.publish_thresholds.epoch_2_receive)
	{
		auto ratio (vban::difficulty::to_multiplier (network_constants.publish_thresholds.epoch_2, threshold_a));
		debug_assert (ratio >= 1);
		multiplier = multiplier * ratio + 1.0 - ratio;
		debug_assert (multiplier >= 1);
	}
	return multiplier;
}

vban::work_pool::work_pool (unsigned max_threads_a, std::chrono::nanoseconds pow_rate_limiter_a, std::function<boost::optional<uint64_t> (vban::work_version const, vban::root const &, uint64_t, std::atomic<int> &)> opencl_a) :
	ticket (0),
	done (false),
	pow_rate_limiter (pow_rate_limiter_a),
	opencl (opencl_a)
{
	static_assert (ATOMIC_INT_LOCK_FREE == 2, "Atomic int needed");
	boost::thread::attributes attrs;
	vban::thread_attributes::set (attrs);
	auto count (network_constants.is_dev_network () ? std::min (max_threads_a, 1u) : std::min (max_threads_a, std::max (1u, boost::thread::hardware_concurrency ())));
	if (opencl)
	{
		// One thread to handle OpenCL
		++count;
	}
	for (auto i (0u); i < count; ++i)
	{
		threads.emplace_back (attrs, [this, i] () {
			vban::thread_role::set (vban::thread_role::name::work);
			vban::work_thread_reprioritize ();
			loop (i);
		});
	}
}

vban::work_pool::~work_pool ()
{
	stop ();
	for (auto & i : threads)
	{
		i.join ();
	}
}

void vban::work_pool::loop (uint64_t thread)
{
	// Quick RNG for work attempts.
	xorshift1024star rng;
	vban::random_pool::generate_block (reinterpret_cast<uint8_t *> (rng.s.data ()), rng.s.size () * sizeof (decltype (rng.s)::value_type));
	uint64_t work;
	uint64_t output;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (output));
	vban::unique_lock<vban::mutex> lock (mutex);
	auto pow_sleep = pow_rate_limiter;
	while (!done)
	{
		auto empty (pending.empty ());
		if (thread == 0)
		{
			// Only work thread 0 notifies work observers
			work_observers.notify (!empty);
		}
		if (!empty)
		{
			auto current_l (pending.front ());
			int ticket_l (ticket);
			lock.unlock ();
			output = 0;
			boost::optional<uint64_t> opt_work;
			if (thread == 0 && opencl)
			{
				opt_work = opencl (current_l.version, current_l.item, current_l.difficulty, ticket);
			}
			if (opt_work.is_initialized ())
			{
				work = *opt_work;
				output = vban::work_v1::value (current_l.item, work);
			}
			else
			{
				// ticket != ticket_l indicates a different thread found a solution and we should stop
				while (ticket == ticket_l && output < current_l.difficulty)
				{
					// Don't query main memory every iteration in order to reduce memory bus traffic
					// All operations here operate on stack memory
					// Count iterations down to zero since comparing to zero is easier than comparing to another number
					unsigned iteration (256);
					while (iteration && output < current_l.difficulty)
					{
						work = rng.next ();
						blake2b_update (&hash, reinterpret_cast<uint8_t *> (&work), sizeof (work));
						blake2b_update (&hash, current_l.item.bytes.data (), current_l.item.bytes.size ());
						blake2b_final (&hash, reinterpret_cast<uint8_t *> (&output), sizeof (output));
						blake2b_init (&hash, sizeof (output));
						iteration -= 1;
					}

					// Add a rate limiter (if specified) to the pow calculation to save some CPUs which don't want to operate at full throttle
					if (pow_sleep != std::chrono::nanoseconds (0))
					{
						std::this_thread::sleep_for (pow_sleep);
					}
				}
			}
			lock.lock ();
			if (ticket == ticket_l)
			{
				// If the ticket matches what we started with, we're the ones that found the solution
				debug_assert (output >= current_l.difficulty);
				debug_assert (current_l.difficulty == 0 || vban::work_v1::value (current_l.item, work) == output);
				// Signal other threads to stop their work next time they check ticket
				++ticket;
				pending.pop_front ();
				lock.unlock ();
				current_l.callback (work);
				lock.lock ();
			}
			else
			{
				// A different thread found a solution
			}
		}
		else
		{
			// Wait for a work request
			producer_condition.wait (lock);
		}
	}
}

void vban::work_pool::cancel (vban::root const & root_a)
{
	vban::lock_guard<vban::mutex> lock (mutex);
	if (!done)
	{
		if (!pending.empty ())
		{
			if (pending.front ().item == root_a)
			{
				++ticket;
			}
		}
		pending.remove_if ([&root_a] (decltype (pending)::value_type const & item_a) {
			bool result{ false };
			if (item_a.item == root_a)
			{
				if (item_a.callback)
				{
					item_a.callback (boost::none);
				}
				result = true;
			}
			return result;
		});
	}
}

void vban::work_pool::stop ()
{
	{
		vban::lock_guard<vban::mutex> lock (mutex);
		done = true;
		++ticket;
	}
	producer_condition.notify_all ();
}

void vban::work_pool::generate (vban::work_version const version_a, vban::root const & root_a, uint64_t difficulty_a, std::function<void (boost::optional<uint64_t> const &)> callback_a)
{
	debug_assert (!root_a.is_zero ());
	if (!threads.empty ())
	{
		{
			vban::lock_guard<vban::mutex> lock (mutex);
			pending.emplace_back (version_a, root_a, difficulty_a, callback_a);
		}
		producer_condition.notify_all ();
	}
	else if (callback_a)
	{
		callback_a (boost::none);
	}
}

boost::optional<uint64_t> vban::work_pool::generate (vban::root const & root_a)
{
	static vban::network_constants network_constants;
	debug_assert (network_constants.is_dev_network ());
	return generate (vban::work_version::work_1, root_a, network_constants.publish_thresholds.base);
}

boost::optional<uint64_t> vban::work_pool::generate (vban::root const & root_a, uint64_t difficulty_a)
{
	static vban::network_constants network_constants;
	debug_assert (network_constants.is_dev_network ());
	return generate (vban::work_version::work_1, root_a, difficulty_a);
}

boost::optional<uint64_t> vban::work_pool::generate (vban::work_version const version_a, vban::root const & root_a, uint64_t difficulty_a)
{
	boost::optional<uint64_t> result;
	if (!threads.empty ())
	{
		std::promise<boost::optional<uint64_t>> work;
		std::future<boost::optional<uint64_t>> future = work.get_future ();
		generate (version_a, root_a, difficulty_a, [&work] (boost::optional<uint64_t> work_a) {
			work.set_value (work_a);
		});
		result = future.get ().value ();
	}
	return result;
}

size_t vban::work_pool::size ()
{
	vban::lock_guard<vban::mutex> lock (mutex);
	return pending.size ();
}

std::unique_ptr<vban::container_info_component> vban::collect_container_info (work_pool & work_pool, std::string const & name)
{
	size_t count;
	{
		vban::lock_guard<vban::mutex> guard (work_pool.mutex);
		count = work_pool.pending.size ();
	}
	auto sizeof_element = sizeof (decltype (work_pool.pending)::value_type);
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "pending", count, sizeof_element }));
	composite->add_component (collect_container_info (work_pool.work_observers, "work_observers"));
	return composite;
}
