#include <vban/lib/memory.hpp>
#include <vban/node/common.hpp>

#include <gtest/gtest.h>
namespace vban
{
void cleanup_dev_directories_on_exit ();
void force_vban_dev_network ();
}

int main (int argc, char ** argv)
{
	vban::force_vban_dev_network ();
	vban::set_use_memory_pools (false);
	vban::node_singleton_memory_pool_purge_guard cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	vban::cleanup_dev_directories_on_exit ();
	return res;
}
