#include <vban/crypto_lib/random_pool.hpp>
#include <vban/node/bootstrap/bootstrap.hpp>
#include <vban/node/bootstrap/bootstrap_attempt.hpp>
#include <vban/node/bootstrap/bootstrap_bulk_push.hpp>
#include <vban/node/bootstrap/bootstrap_frontier.hpp>
#include <vban/node/common.hpp>
#include <vban/node/node.hpp>
#include <vban/node/transport/tcp.hpp>
#include <vban/node/websocket.hpp>

#include <boost/format.hpp>

#include <algorithm>

constexpr unsigned vban::bootstrap_limits::requeued_pulls_limit;
constexpr unsigned vban::bootstrap_limits::requeued_pulls_limit_dev;

vban::bootstrap_attempt::bootstrap_attempt (std::shared_ptr<vban::node> const & node_a, vban::bootstrap_mode mode_a, uint64_t incremental_id_a, std::string id_a) :
	node (node_a),
	incremental_id (incremental_id_a),
	id (id_a),
	mode (mode_a)
{
	if (id.empty ())
	{
		vban::random_constants constants;
		id = constants.random_128.to_string ();
	}
	node->logger.always_log (boost::str (boost::format ("Starting %1% bootstrap attempt with ID %2%") % mode_text () % id));
	node->bootstrap_initiator.notify_listeners (true);
	if (node->websocket_server)
	{
		vban::websocket::message_builder builder;
		node->websocket_server->broadcast (builder.bootstrap_started (id, mode_text ()));
	}
}

vban::bootstrap_attempt::~bootstrap_attempt ()
{
	node->logger.always_log (boost::str (boost::format ("Exiting %1% bootstrap attempt with ID %2%") % mode_text () % id));
	node->bootstrap_initiator.notify_listeners (false);
	if (node->websocket_server)
	{
		vban::websocket::message_builder builder;
		node->websocket_server->broadcast (builder.bootstrap_exited (id, mode_text (), attempt_start, total_blocks));
	}
}

bool vban::bootstrap_attempt::should_log ()
{
	vban::lock_guard<vban::mutex> guard (next_log_mutex);
	auto result (false);
	auto now (std::chrono::steady_clock::now ());
	if (next_log < now)
	{
		result = true;
		next_log = now + std::chrono::seconds (15);
	}
	return result;
}

bool vban::bootstrap_attempt::still_pulling ()
{
	debug_assert (!mutex.try_lock ());
	auto running (!stopped);
	auto still_pulling (pulling > 0);
	return running && still_pulling;
}

void vban::bootstrap_attempt::pull_started ()
{
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		++pulling;
	}
	condition.notify_all ();
}

void vban::bootstrap_attempt::pull_finished ()
{
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		--pulling;
	}
	condition.notify_all ();
}

void vban::bootstrap_attempt::stop ()
{
	{
		vban::lock_guard<vban::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
	node->bootstrap_initiator.connections->clear_pulls (incremental_id);
}

std::string vban::bootstrap_attempt::mode_text ()
{
	std::string mode_text;
	if (mode == vban::bootstrap_mode::legacy)
	{
		mode_text = "legacy";
	}
	else if (mode == vban::bootstrap_mode::lazy)
	{
		mode_text = "lazy";
	}
	else if (mode == vban::bootstrap_mode::wallet_lazy)
	{
		mode_text = "wallet_lazy";
	}
	return mode_text;
}

void vban::bootstrap_attempt::add_frontier (vban::pull_info const &)
{
	debug_assert (mode == vban::bootstrap_mode::legacy);
}

void vban::bootstrap_attempt::add_bulk_push_target (vban::block_hash const &, vban::block_hash const &)
{
	debug_assert (mode == vban::bootstrap_mode::legacy);
}

bool vban::bootstrap_attempt::request_bulk_push_target (std::pair<vban::block_hash, vban::block_hash> &)
{
	debug_assert (mode == vban::bootstrap_mode::legacy);
	return true;
}

void vban::bootstrap_attempt::set_start_account (vban::account const &)
{
	debug_assert (mode == vban::bootstrap_mode::legacy);
}

bool vban::bootstrap_attempt::process_block (std::shared_ptr<vban::block> const & block_a, vban::account const & known_account_a, uint64_t pull_blocks_processed, vban::bulk_pull::count_t max_blocks, bool block_expected, unsigned retry_limit)
{
	bool stop_pull (false);
	// If block already exists in the ledger, then we can avoid next part of long account chain
	if (pull_blocks_processed % vban::bootstrap_limits::pull_count_per_check == 0 && node->ledger.block_or_pruned_exists (block_a->hash ()))
	{
		stop_pull = true;
	}
	else
	{
		vban::unchecked_info info (block_a, known_account_a, 0, vban::signature_verification::unknown);
		node->block_processor.add (info);
	}
	return stop_pull;
}

bool vban::bootstrap_attempt::lazy_start (vban::hash_or_account const &, bool)
{
	debug_assert (mode == vban::bootstrap_mode::lazy);
	return false;
}

void vban::bootstrap_attempt::lazy_add (vban::pull_info const &)
{
	debug_assert (mode == vban::bootstrap_mode::lazy);
}

void vban::bootstrap_attempt::lazy_requeue (vban::block_hash const &, vban::block_hash const &, bool)
{
	debug_assert (mode == vban::bootstrap_mode::lazy);
}

uint32_t vban::bootstrap_attempt::lazy_batch_size ()
{
	debug_assert (mode == vban::bootstrap_mode::lazy);
	return node->network_params.bootstrap.lazy_min_pull_blocks;
}

bool vban::bootstrap_attempt::lazy_processed_or_exists (vban::block_hash const &)
{
	debug_assert (mode == vban::bootstrap_mode::lazy);
	return false;
}

bool vban::bootstrap_attempt::lazy_has_expired () const
{
	debug_assert (mode == vban::bootstrap_mode::lazy);
	return true;
}

void vban::bootstrap_attempt::requeue_pending (vban::account const &)
{
	debug_assert (mode == vban::bootstrap_mode::wallet_lazy);
}

void vban::bootstrap_attempt::wallet_start (std::deque<vban::account> &)
{
	debug_assert (mode == vban::bootstrap_mode::wallet_lazy);
}

size_t vban::bootstrap_attempt::wallet_size ()
{
	debug_assert (mode == vban::bootstrap_mode::wallet_lazy);
	return 0;
}
