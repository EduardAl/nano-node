namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace vban
{
class node_flags;
}
namespace vban_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &, vban::node_flags const & flags);
};
}
