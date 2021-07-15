#include "gtest/gtest.h"

#include <vban/node/common.hpp>
#include <vban/node/logging.hpp>

#include <boost/filesystem/path.hpp>

namespace vban
{
void cleanup_dev_directories_on_exit ();
void force_vban_dev_network ();
boost::filesystem::path unique_path ();
}

GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	vban::force_vban_dev_network ();
	vban::node_singleton_memory_pool_purge_guard memory_pool_cleanup_guard;
	// Setting up logging so that there aren't any piped to standard output.
	vban::logging logging;
	logging.init (vban::unique_path ());
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	vban::cleanup_dev_directories_on_exit ();
	return res;
}
