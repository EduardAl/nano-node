#include <vban/lib/config.hpp>
#include <vban/lib/utility.hpp>
#include <vban/node/write_database_queue.hpp>

#include <algorithm>

vban::write_guard::write_guard (std::function<void ()> guard_finish_callback_a) :
	guard_finish_callback (guard_finish_callback_a)
{
}

vban::write_guard::write_guard (vban::write_guard && write_guard_a) noexcept :
	guard_finish_callback (std::move (write_guard_a.guard_finish_callback)),
	owns (write_guard_a.owns)
{
	write_guard_a.owns = false;
	write_guard_a.guard_finish_callback = nullptr;
}

vban::write_guard & vban::write_guard::operator= (vban::write_guard && write_guard_a) noexcept
{
	owns = write_guard_a.owns;
	guard_finish_callback = std::move (write_guard_a.guard_finish_callback);

	write_guard_a.owns = false;
	write_guard_a.guard_finish_callback = nullptr;
	return *this;
}

vban::write_guard::~write_guard ()
{
	if (owns)
	{
		guard_finish_callback ();
	}
}

bool vban::write_guard::is_owned () const
{
	return owns;
}

void vban::write_guard::release ()
{
	debug_assert (owns);
	if (owns)
	{
		guard_finish_callback ();
	}
	owns = false;
}

vban::write_database_queue::write_database_queue (bool use_noops_a) :
	guard_finish_callback ([use_noops_a, &queue = queue, &mutex = mutex, &cv = cv] () {
		if (!use_noops_a)
		{
			{
				vban::lock_guard<vban::mutex> guard (mutex);
				queue.pop_front ();
			}
			cv.notify_all ();
		}
	}),
	use_noops (use_noops_a)
{
}

vban::write_guard vban::write_database_queue::wait (vban::writer writer)
{
	if (use_noops)
	{
		return write_guard ([] {});
	}

	vban::unique_lock<vban::mutex> lk (mutex);
	// Add writer to the end of the queue if it's not already waiting
	auto exists = std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
	if (!exists)
	{
		queue.push_back (writer);
	}

	while (queue.front () != writer)
	{
		cv.wait (lk);
	}

	return write_guard (guard_finish_callback);
}

bool vban::write_database_queue::contains (vban::writer writer)
{
	debug_assert (!use_noops && vban::network_constants ().is_dev_network ());
	vban::lock_guard<vban::mutex> guard (mutex);
	return std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
}

bool vban::write_database_queue::process (vban::writer writer)
{
	if (use_noops)
	{
		return true;
	}

	auto result = false;
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		// Add writer to the end of the queue if it's not already waiting
		auto exists = std::find (queue.cbegin (), queue.cend (), writer) != queue.cend ();
		if (!exists)
		{
			queue.push_back (writer);
		}

		result = (queue.front () == writer);
	}

	if (!result)
	{
		cv.notify_all ();
	}

	return result;
}

vban::write_guard vban::write_database_queue::pop ()
{
	return write_guard (guard_finish_callback);
}
