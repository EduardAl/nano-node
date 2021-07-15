#include <vban/lib/errors.hpp>
#include <vban/lib/utility.hpp>
#include <vban/node/cli.hpp>
#include <vban/rpc/rpc.hpp>
#include <vban/secure/utility.hpp>
#include <vban/secure/working.hpp>

#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>

int main (int argc, char * const * argv)
{
	vban::set_umask ();
	try
	{
		boost::program_options::options_description description ("Command line options");
		description.add_options () ("help", "Print out options");
		vban::add_node_options (description);
		boost::program_options::variables_map vm;
		boost::program_options::store (boost::program_options::command_line_parser (argc, argv).options (description).allow_unregistered ().run (), vm);
		boost::program_options::notify (vm);
		int result (0);

		auto ec = vban::handle_node_options (vm);
		if (ec == vban::error_cli::unknown_command && vm.count ("help") != 0)
		{
			std::cout << description << std::endl;
		}
		return result;
	}
	catch (std::exception const & e)
	{
		std::cerr << boost::str (boost::format ("Exception while initializing %1%") % e.what ());
	}
	catch (...)
	{
		std::cerr << boost::str (boost::format ("Unknown exception while initializing"));
	}
	return 1;
}
