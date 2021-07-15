#pragma once

#include <vban/lib/numbers.hpp>
#include <vban/node/active_transactions.hpp>
#include <vban/node/prioritization.hpp>

#include <boost/optional.hpp>

#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

namespace vban
{
class block;
class node;
class election_scheduler final
{
public:
	election_scheduler (vban::node & node);
	~election_scheduler ();
	// Manualy start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void manual (std::shared_ptr<vban::block> const &, boost::optional<vban::uint256_t> const & = boost::none, vban::election_behavior = vban::election_behavior::normal, std::function<void (std::shared_ptr<vban::block> const &)> const & = nullptr);
	// Activates the first unconfirmed block of \p account_a
	void activate (vban::account const &, vban::transaction const &);
	void stop ();
	// Blocks until no more elections can be activated or there are no more elections to activate
	void flush ();
	void notify ();
	size_t size () const;
	bool empty () const;
	size_t priority_queue_size () const;

private:
	void run ();
	bool empty_locked () const;
	bool priority_queue_predicate () const;
	bool manual_queue_predicate () const;
	bool overfill_predicate () const;
	vban::prioritization priority;
	std::deque<std::tuple<std::shared_ptr<vban::block>, boost::optional<vban::uint256_t>, vban::election_behavior, std::function<void (std::shared_ptr<vban::block>)>>> manual_queue;
	vban::node & node;
	bool stopped;
	vban::condition_variable condition;
	mutable vban::mutex mutex;
	std::thread thread;
};
}
