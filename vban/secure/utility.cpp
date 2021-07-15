#include <vban/lib/config.hpp>
#include <vban/secure/utility.hpp>
#include <vban/secure/working.hpp>

#include <boost/filesystem.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path vban::working_path (bool legacy)
{
	static vban::network_constants network_constants;
	auto result (vban::app_path ());
	switch (network_constants.network ())
	{
		case vban::vban_networks::vban_dev_network:
			if (!legacy)
			{
				result /= "VbanDev";
			}
			else
			{
				result /= "RaiBlocksDev";
			}
			break;
		case vban::vban_networks::vban_beta_network:
			if (!legacy)
			{
				result /= "VbanBeta";
			}
			else
			{
				result /= "RaiBlocksBeta";
			}
			break;
		case vban::vban_networks::vban_live_network:
			if (!legacy)
			{
				result /= "Vban";
			}
			else
			{
				result /= "RaiBlocks";
			}
			break;
		case vban::vban_networks::vban_test_network:
			if (!legacy)
			{
				result /= "VbanTest";
			}
			else
			{
				result /= "RaiBlocksTest";
			}
			break;
	}
	return result;
}

boost::filesystem::path vban::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

void vban::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		boost::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		boost::filesystem::remove (lockfile, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary lock file: " << ec.message () << std::endl;
		}
	}
}

namespace vban
{
/** A wrapper for handling signals */
std::function<void ()> signal_handler_impl;
void signal_handler (int sig)
{
	if (signal_handler_impl != nullptr)
	{
		signal_handler_impl ();
	}
}
}
