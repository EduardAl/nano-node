#pragma once

#include <vban/lib/locks.hpp>
#include <vban/secure/common.hpp>

#include <deque>
#include <functional>
#include <thread>

namespace vban
{
class epochs;
class logger_mt;
class node_config;
class signature_checker;

class state_block_signature_verification
{
public:
	state_block_signature_verification (vban::signature_checker &, vban::epochs &, vban::node_config &, vban::logger_mt &, uint64_t);
	~state_block_signature_verification ();
	void add (vban::unchecked_info const & info_a);
	size_t size ();
	void stop ();
	bool is_active ();

	std::function<void (std::deque<vban::unchecked_info> &, std::vector<int> const &, std::vector<vban::block_hash> const &, std::vector<vban::signature> const &)> blocks_verified_callback;
	std::function<void ()> transition_inactive_callback;

private:
	vban::signature_checker & signature_checker;
	vban::epochs & epochs;
	vban::node_config & node_config;
	vban::logger_mt & logger;

	vban::mutex mutex{ mutex_identifier (mutexes::state_block_signature_verification) };
	bool stopped{ false };
	bool active{ false };
	std::deque<vban::unchecked_info> state_blocks;
	vban::condition_variable condition;
	std::thread thread;

	void run (uint64_t block_processor_verification_size);
	std::deque<vban::unchecked_info> setup_items (size_t);
	void verify_state_blocks (std::deque<vban::unchecked_info> &);
};

std::unique_ptr<vban::container_info_component> collect_container_info (state_block_signature_verification & state_block_signature_verification, std::string const & name);
}
