#include <vban/lib/ipc.hpp>
#include <vban/lib/utility.hpp>

vban::ipc::socket_base::socket_base (boost::asio::io_context & io_ctx_a) :
	io_timer (io_ctx_a)
{
}

void vban::ipc::socket_base::timer_start (std::chrono::seconds timeout_a)
{
	if (timeout_a < std::chrono::seconds::max ())
	{
		io_timer.expires_from_now (boost::posix_time::seconds (static_cast<long> (timeout_a.count ())));
		io_timer.async_wait ([this] (const boost::system::error_code & ec) {
			if (!ec)
			{
				this->timer_expired ();
			}
		});
	}
}

void vban::ipc::socket_base::timer_expired ()
{
	close ();
}

void vban::ipc::socket_base::timer_cancel ()
{
	boost::system::error_code ec;
	io_timer.cancel (ec);
	debug_assert (!ec);
}

vban::ipc::dsock_file_remover::dsock_file_remover (std::string const & file_a) :
	filename (file_a)
{
	std::remove (filename.c_str ());
}

vban::ipc::dsock_file_remover::~dsock_file_remover ()
{
	std::remove (filename.c_str ());
}
