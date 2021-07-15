#include <vban/lib/config.hpp>
#include <vban/lib/locks.hpp>
#include <vban/lib/utility.hpp>

#include <boost/format.hpp>

#include <cstring>
#include <iostream>

#if USING_VBAN_TIMED_LOCKS
namespace vban
{
// These mutexes must have std::mutex interface in addition to "const char* get_name ()" method
template <typename Mutex>
void output (const char * str, std::chrono::milliseconds time, Mutex & mutex)
{
	static vban::mutex cout_mutex;
	auto stacktrace = vban::generate_stacktrace ();
	// Guard standard out to keep the output from being interleaved
	std::lock_guard guard (cout_mutex);
	std::cout << (boost::format ("%1% Mutex %2% %3% for %4%ms\n%5%") % std::addressof (mutex) % mutex.get_name () % str % time.count () % stacktrace).str ()
			  << std::endl;
}

template <typename Mutex>
void output_if_held_long_enough (vban::timer<std::chrono::milliseconds> & timer, Mutex & mutex)
{
	auto time_held = timer.since_start ();
	if (time_held >= std::chrono::milliseconds (VBAN_TIMED_LOCKS))
	{
		std::unique_lock lk (vban::mutex_to_filter_mutex);
		if (!vban::any_filters_registered () || (vban::mutex_to_filter == &mutex))
		{
			lk.unlock ();
			output ("held", time_held, mutex);
		}
	}
	if (timer.current_state () != vban::timer_state::stopped)
	{
		timer.stop ();
	}
}

#ifndef VBAN_TIMED_LOCKS_IGNORE_BLOCKED
template <typename Mutex>
void output_if_blocked_long_enough (vban::timer<std::chrono::milliseconds> & timer, Mutex & mutex)
{
	auto time_blocked = timer.since_start ();
	if (time_blocked >= std::chrono::milliseconds (VBAN_TIMED_LOCKS))
	{
		std::unique_lock lk (vban::mutex_to_filter_mutex);
		if (!vban::any_filters_registered () || (vban::mutex_to_filter == &mutex))
		{
			lk.unlock ();
			output ("blocked", time_blocked, mutex);
		}
	}
}
#endif

lock_guard<vban::mutex>::lock_guard (vban::mutex & mutex) :
	mut (mutex)
{
	timer.start ();

	mut.lock ();
#ifndef VBAN_TIMED_LOCKS_IGNORE_BLOCKED
	output_if_blocked_long_enough (timer, mut);
#endif
}

lock_guard<vban::mutex>::~lock_guard () noexcept
{
	mut.unlock ();
	output_if_held_long_enough (timer, mut);
}

template <typename Mutex, typename U>
unique_lock<Mutex, U>::unique_lock (Mutex & mutex) :
	mut (std::addressof (mutex))
{
	lock_impl ();
}

template <typename Mutex, typename U>
unique_lock<Mutex, U>::unique_lock (Mutex & mutex, std::defer_lock_t) noexcept :
	mut (std::addressof (mutex))
{
}

template <typename Mutex, typename U>
void unique_lock<Mutex, U>::lock_impl ()
{
	timer.start ();

	mut->lock ();
	owns = true;
#ifndef VBAN_TIMED_LOCKS_IGNORE_BLOCKED
	output_if_blocked_long_enough (timer, *mut);
#endif
}

template <typename Mutex, typename U>
unique_lock<Mutex, U> & unique_lock<Mutex, U>::operator= (unique_lock<Mutex, U> && other) noexcept
{
	if (this != std::addressof (other))
	{
		if (owns)
		{
			mut->unlock ();
			owns = false;

			output_if_held_long_enough (timer, *mut);
		}

		mut = other.mut;
		owns = other.owns;
		timer = other.timer;

		other.mut = nullptr;
		other.owns = false;
	}
	return *this;
}

template <typename Mutex, typename U>
unique_lock<Mutex, U>::~unique_lock () noexcept
{
	if (owns)
	{
		mut->unlock ();
		owns = false;

		output_if_held_long_enough (timer, *mut);
	}
}

template <typename Mutex, typename U>
void unique_lock<Mutex, U>::lock ()
{
	validate ();
	lock_impl ();
}

template <typename Mutex, typename U>
bool unique_lock<Mutex, U>::try_lock ()
{
	validate ();
	owns = mut->try_lock ();

	if (owns)
	{
		timer.start ();
	}

	return owns;
}

template <typename Mutex, typename U>
void unique_lock<Mutex, U>::unlock ()
{
	if (!mut || !owns)
	{
		throw (std::system_error (std::make_error_code (std::errc::operation_not_permitted)));
	}

	mut->unlock ();
	owns = false;

	output_if_held_long_enough (timer, *mut);
}

template <typename Mutex, typename U>
bool unique_lock<Mutex, U>::owns_lock () const noexcept
{
	return owns;
}

template <typename Mutex, typename U>
unique_lock<Mutex, U>::operator bool () const noexcept
{
	return owns;
}

template <typename Mutex, typename U>
Mutex * unique_lock<Mutex, U>::mutex () const noexcept
{
	return mut;
}

template <typename Mutex, typename U>
void unique_lock<Mutex, U>::validate () const
{
	if (!mut)
	{
		throw (std::system_error (std::make_error_code (std::errc::operation_not_permitted)));
	}

	if (owns)
	{
		throw (std::system_error (std::make_error_code (std::errc::resource_deadlock_would_occur)));
	}
}

// Explicit instantiations for allowed types
template class unique_lock<vban::mutex>;

void condition_variable::notify_one () noexcept
{
	cnd.notify_one ();
}

void condition_variable::notify_all () noexcept
{
	cnd.notify_all ();
}

void condition_variable::wait (vban::unique_lock<vban::mutex> & lk)
{
	if (!lk.mut || !lk.owns)
	{
		throw (std::system_error (std::make_error_code (std::errc::operation_not_permitted)));
	}

	output_if_held_long_enough (lk.timer, *lk.mut);
	// Start again in case cnd.wait calls unique_lock::lock/unlock () depending on some implementations
	lk.timer.start ();
	cnd.wait (lk);
	lk.timer.restart ();
}

vban::mutex * mutex_to_filter{ nullptr };
vban::mutex mutex_to_filter_mutex;

bool should_be_filtered (const char * name)
{
	return std::strcmp (name, xstr (VBAN_TIMED_LOCKS_FILTER)) == 0;
}

bool any_filters_registered ()
{
	return std::strcmp ("", xstr (VBAN_TIMED_LOCKS_FILTER)) != 0;
}
}
#endif

char const * vban::mutex_identifier (mutexes mutex)
{
	switch (mutex)
	{
		case mutexes::active:
			return "active";
		case mutexes::block_arrival:
			return "block_arrival";
		case mutexes::block_processor:
			return "block_processor";
		case mutexes::block_uniquer:
			return "block_uniquer";
		case mutexes::blockstore_cache:
			return "blockstore_cache";
		case mutexes::confirmation_height_processor:
			return "confirmation_height_processor";
		case mutexes::election_winner_details:
			return "election_winner_details";
		case mutexes::gap_cache:
			return "gap_cache";
		case mutexes::network_filter:
			return "network_filter";
		case mutexes::observer_set:
			return "observer_set";
		case mutexes::request_aggregator:
			return "request_aggregator";
		case mutexes::state_block_signature_verification:
			return "state_block_signature_verification";
		case mutexes::telemetry:
			return "telemetry";
		case mutexes::vote_generator:
			return "vote_generator";
		case mutexes::vote_processor:
			return "vote_processor";
		case mutexes::vote_uniquer:
			return "vote_uniquer";
		case mutexes::votes_cache:
			return "votes_cache";
		case mutexes::work_pool:
			return "work_pool";
	}

	throw std::runtime_error ("Invalid mutexes enum specified");
}