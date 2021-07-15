#pragma once

#include <vban/lib/locks.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/lib/utility.hpp>
#include <vban/node/wallet.hpp>
#include <vban/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace mi = boost::multi_index;

namespace vban
{
class ledger;
class network;
class node_config;
class stat;
class vote_processor;
class wallets;
namespace transport
{
	class channel;
}

class vote_spacing final
{
	class entry
	{
	public:
		vban::root root;
		std::chrono::steady_clock::time_point time;
		vban::block_hash hash;
	};

	boost::multi_index_container<entry,
	mi::indexed_by<
	mi::hashed_non_unique<mi::tag<class tag_root>,
	mi::member<entry, vban::root, &entry::root>>,
	mi::ordered_non_unique<mi::tag<class tag_time>,
	mi::member<entry, std::chrono::steady_clock::time_point, &entry::time>>>>
	recent;
	std::chrono::milliseconds const delay;
	void trim ();

public:
	vote_spacing (std::chrono::milliseconds const & delay) :
		delay{ delay }
	{
	}
	bool votable (vban::root const & root_a, vban::block_hash const & hash_a) const;
	void flag (vban::root const & root_a, vban::block_hash const & hash_a);
	size_t size () const;
};

class local_vote_history final
{
	class local_vote final
	{
	public:
		local_vote (vban::root const & root_a, vban::block_hash const & hash_a, std::shared_ptr<vban::vote> const & vote_a) :
			root (root_a),
			hash (hash_a),
			vote (vote_a)
		{
		}
		vban::root root;
		vban::block_hash hash;
		std::shared_ptr<vban::vote> vote;
	};

public:
	local_vote_history (vban::voting_constants const & constants) :
		constants{ constants }
	{
	}
	void add (vban::root const & root_a, vban::block_hash const & hash_a, std::shared_ptr<vban::vote> const & vote_a);
	void erase (vban::root const & root_a);

	std::vector<std::shared_ptr<vban::vote>> votes (vban::root const & root_a, vban::block_hash const & hash_a, bool const is_final_a = false) const;
	bool exists (vban::root const &) const;
	size_t size () const;

private:
	// clang-format off
	boost::multi_index_container<local_vote,
	mi::indexed_by<
		mi::hashed_non_unique<mi::tag<class tag_root>,
			mi::member<local_vote, vban::root, &local_vote::root>>,
		mi::sequenced<mi::tag<class tag_sequence>>>>
	history;
	// clang-format on

	vban::voting_constants const & constants;
	void clean ();
	std::vector<std::shared_ptr<vban::vote>> votes (vban::root const & root_a) const;
	// Only used in Debug
	bool consistency_check (vban::root const &) const;
	mutable vban::mutex mutex;

	friend std::unique_ptr<container_info_component> collect_container_info (local_vote_history & history, std::string const & name);
	friend class local_vote_history_basic_Test;
};

std::unique_ptr<container_info_component> collect_container_info (local_vote_history & history, std::string const & name);

class vote_generator final
{
private:
	using candidate_t = std::pair<vban::root, vban::block_hash>;
	using request_t = std::pair<std::vector<candidate_t>, std::shared_ptr<vban::transport::channel>>;

public:
	vote_generator (vban::node_config const & config_a, vban::ledger & ledger_a, vban::wallets & wallets_a, vban::vote_processor & vote_processor_a, vban::local_vote_history & history_a, vban::network & network_a, vban::stat & stats_a, bool is_final_a);
	/** Queue items for vote generation, or broadcast votes already in cache */
	void add (vban::root const &, vban::block_hash const &);
	/** Queue blocks for vote generation, returning the number of successful candidates.*/
	size_t generate (std::vector<std::shared_ptr<vban::block>> const & blocks_a, std::shared_ptr<vban::transport::channel> const & channel_a);
	void set_reply_action (std::function<void (std::shared_ptr<vban::vote> const &, std::shared_ptr<vban::transport::channel> const &)>);
	void stop ();

private:
	void run ();
	void broadcast (vban::unique_lock<vban::mutex> &);
	void reply (vban::unique_lock<vban::mutex> &, request_t &&);
	void vote (std::vector<vban::block_hash> const &, std::vector<vban::root> const &, std::function<void (std::shared_ptr<vban::vote> const &)> const &);
	void broadcast_action (std::shared_ptr<vban::vote> const &) const;
	std::function<void (std::shared_ptr<vban::vote> const &, std::shared_ptr<vban::transport::channel> &)> reply_action; // must be set only during initialization by using set_reply_action
	vban::node_config const & config;
	vban::ledger & ledger;
	vban::wallets & wallets;
	vban::vote_processor & vote_processor;
	vban::local_vote_history & history;
	vban::vote_spacing spacing;
	vban::network & network;
	vban::stat & stats;
	mutable vban::mutex mutex;
	vban::condition_variable condition;
	static size_t constexpr max_requests{ 2048 };
	std::deque<request_t> requests;
	std::deque<candidate_t> candidates;
	vban::network_params network_params;
	std::atomic<bool> stopped{ false };
	bool started{ false };
	std::thread thread;
	bool is_final;

	friend std::unique_ptr<container_info_component> collect_container_info (vote_generator & vote_generator, std::string const & name);
};

std::unique_ptr<container_info_component> collect_container_info (vote_generator & generator, std::string const & name);

class vote_generator_session final
{
public:
	vote_generator_session (vote_generator & vote_generator_a);
	void add (vban::root const &, vban::block_hash const &);
	void flush ();

private:
	vban::vote_generator & generator;
	std::vector<std::pair<vban::root, vban::block_hash>> items;
};
}
