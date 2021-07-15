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
	vban::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	vban::cleanup_dev_directories_on_exit ();
	return res;
}
