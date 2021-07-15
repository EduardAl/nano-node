#include <vban/crypto_lib/random_pool.hpp>
#include <vban/node/testing.hpp>
#include <vban/test_common/testutil.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <numeric>

using namespace std::chrono_literals;

/* Convenience constants for tests which are always on the test network */
namespace
{
vban::ledger_constants dev_constants (vban::vban_networks::vban_dev_network);
}

vban::keypair const & vban::zero_key (dev_constants.zero_key);
vban::keypair const & vban::dev_genesis_key (dev_constants.dev_genesis_key);
vban::account const & vban::vban_dev_account (dev_constants.vban_dev_account);
std::string const & vban::vban_dev_genesis (dev_constants.vban_dev_genesis);
vban::account const & vban::genesis_account (dev_constants.genesis_account);
vban::block_hash const & vban::genesis_hash (dev_constants.genesis_hash);
vban::uint256_t const & vban::genesis_amount (dev_constants.genesis_amount);
vban::account const & vban::burn_account (dev_constants.burn_account);

void vban::wait_peer_connections (vban::system & system_a)
{
	auto wait_peer_count = [&system_a] (bool in_memory) {
		auto num_nodes = system_a.nodes.size ();
		system_a.deadline_set (20s);
		size_t peer_count = 0;
		while (peer_count != num_nodes * (num_nodes - 1))
		{
			ASSERT_NO_ERROR (system_a.poll ());
			peer_count = std::accumulate (system_a.nodes.cbegin (), system_a.nodes.cend (), std::size_t{ 0 }, [in_memory] (auto total, auto const & node) {
				if (in_memory)
				{
					return total += node->network.size ();
				}
				else
				{
					auto transaction = node->store.tx_begin_read ();
					return total += node->store.peer_count (transaction);
				}
			});
		}
	};

	// Do a pre-pass with in-memory containers to reduce IO if still in the process of connecting to peers
	wait_peer_count (true);
	wait_peer_count (false);
}
