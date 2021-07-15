#include <vban/node/election_scheduler.hpp>
#include <vban/node/node.hpp>

vban::election_scheduler::election_scheduler (vban::node & node) :
	node{ node },
	stopped{ false },
	thread{ [this] () { run (); } }
{
}

vban::election_scheduler::~election_scheduler ()
{
	stop ();
	thread.join ();
}

void vban::election_scheduler::manual (std::shared_ptr<vban::block> const & block_a, boost::optional<vban::uint256_t> const & previous_balance_a, vban::election_behavior election_behavior_a, std::function<void (std::shared_ptr<vban::block> const &)> const & confirmation_action_a)
{
	vban::lock_guard<vban::mutex> lock{ mutex };
	manual_queue.push_back (std::make_tuple (block_a, previous_balance_a, election_behavior_a, confirmation_action_a));
	notify ();
}

void vban::election_scheduler::activate (vban::account const & account_a, vban::transaction const & transaction)
{
	debug_assert (!account_a.is_zero ());
	vban::account_info account_info;
	if (!node.store.account_get (transaction, account_a, account_info))
	{
		vban::confirmation_height_info conf_info;
		node.store.confirmation_height_get (transaction, account_a, conf_info);
		if (conf_info.height < account_info.block_count)
		{
			debug_assert (conf_info.frontier != account_info.head);
			auto hash = conf_info.height == 0 ? account_info.open_block : node.store.block_successor (transaction, conf_info.frontier);
			auto block = node.store.block_get (transaction, hash);
			debug_assert (block != nullptr);
			if (node.ledger.dependents_confirmed (transaction, *block))
			{
				vban::lock_guard<vban::mutex> lock{ mutex };
				priority.push (account_info.modified, block);
				notify ();
			}
		}
	}
}

void vban::election_scheduler::stop ()
{
	vban::unique_lock<vban::mutex> lock{ mutex };
	stopped = true;
	notify ();
}

void vban::election_scheduler::flush ()
{
	vban::unique_lock<vban::mutex> lock{ mutex };
	condition.wait (lock, [this] () {
		return stopped || empty_locked () || node.active.vacancy () <= 0;
	});
}

void vban::election_scheduler::notify ()
{
	condition.notify_all ();
}

size_t vban::election_scheduler::size () const
{
	vban::lock_guard<vban::mutex> lock{ mutex };
	return priority.size () + manual_queue.size ();
}

bool vban::election_scheduler::empty_locked () const
{
	return priority.empty () && manual_queue.empty ();
}

bool vban::election_scheduler::empty () const
{
	vban::lock_guard<vban::mutex> lock{ mutex };
	return empty_locked ();
}

size_t vban::election_scheduler::priority_queue_size () const
{
	return priority.size ();
}

bool vban::election_scheduler::priority_queue_predicate () const
{
	return node.active.vacancy () > 0 && !priority.empty ();
}

bool vban::election_scheduler::manual_queue_predicate () const
{
	return !manual_queue.empty ();
}

bool vban::election_scheduler::overfill_predicate () const
{
	return node.active.vacancy () < 0;
}

void vban::election_scheduler::run ()
{
	vban::thread_role::set (vban::thread_role::name::election_scheduler);
	vban::unique_lock<vban::mutex> lock{ mutex };
	while (!stopped)
	{
		condition.wait (lock, [this] () {
			return stopped || priority_queue_predicate () || manual_queue_predicate () || overfill_predicate ();
		});
		debug_assert ((std::this_thread::yield (), true)); // Introduce some random delay in debug builds
		if (!stopped)
		{
			if (overfill_predicate ())
			{
				node.active.erase_oldest ();
			}
			else if (manual_queue_predicate ())
			{
				auto const [block, previous_balance, election_behavior, confirmation_action] = manual_queue.front ();
				vban::unique_lock<vban::mutex> lock2 (node.active.mutex);
				node.active.insert_impl (lock2, block, previous_balance, election_behavior, confirmation_action);
				manual_queue.pop_front ();
			}
			else if (priority_queue_predicate ())
			{
				auto block = priority.top ();
				std::shared_ptr<vban::election> election;
				vban::unique_lock<vban::mutex> lock2 (node.active.mutex);
				election = node.active.insert_impl (lock2, block).election;
				if (election != nullptr)
				{
					election->transition_active ();
				}
				priority.pop ();
			}
			notify ();
		}
	}
}
