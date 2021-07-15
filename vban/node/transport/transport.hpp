#pragma once

#include <vban/lib/locks.hpp>
#include <vban/lib/rate_limiting.hpp>
#include <vban/lib/stats.hpp>
#include <vban/node/common.hpp>
#include <vban/node/socket.hpp>

#include <boost/asio/ip/network_v6.hpp>

namespace vban
{
class bandwidth_limiter final
{
public:
	// initialize with limit 0 = unbounded
	bandwidth_limiter (const double, const size_t);
	bool should_drop (const size_t &);
	void reset (const double, const size_t);

private:
	vban::rate::token_bucket bucket;
};

namespace transport
{
	class message;
	vban::endpoint map_endpoint_to_v6 (vban::endpoint const &);
	vban::endpoint map_tcp_to_endpoint (vban::tcp_endpoint const &);
	vban::tcp_endpoint map_endpoint_to_tcp (vban::endpoint const &);
	boost::asio::ip::address map_address_to_subnetwork (boost::asio::ip::address const &);
	boost::asio::ip::address ipv4_address_or_ipv6_subnet (boost::asio::ip::address const &);
	// Unassigned, reserved, self
	bool reserved_address (vban::endpoint const &, bool = false);
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	enum class transport_type : uint8_t
	{
		undefined = 0,
		udp = 1,
		tcp = 2,
		loopback = 3
	};
	class channel
	{
	public:
		channel (vban::node &);
		virtual ~channel () = default;
		virtual size_t hash_code () const = 0;
		virtual bool operator== (vban::transport::channel const &) const = 0;
		void send (vban::message const & message_a, std::function<void (boost::system::error_code const &, size_t)> const & callback_a = nullptr, vban::buffer_drop_policy policy_a = vban::buffer_drop_policy::limiter);
		virtual void send_buffer (vban::shared_const_buffer const &, std::function<void (boost::system::error_code const &, size_t)> const & = nullptr, vban::buffer_drop_policy = vban::buffer_drop_policy::limiter) = 0;
		virtual std::string to_string () const = 0;
		virtual vban::endpoint get_endpoint () const = 0;
		virtual vban::tcp_endpoint get_tcp_endpoint () const = 0;
		virtual vban::transport::transport_type get_type () const = 0;

		std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			return last_bootstrap_attempt;
		}

		void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			last_bootstrap_attempt = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_received () const
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			return last_packet_received;
		}

		void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			last_packet_received = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_sent () const
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			return last_packet_sent;
		}

		void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			last_packet_sent = time_a;
		}

		boost::optional<vban::account> get_node_id_optional () const
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			return node_id;
		}

		vban::account get_node_id () const
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			if (node_id.is_initialized ())
			{
				return node_id.get ();
			}
			else
			{
				return 0;
			}
		}

		void set_node_id (vban::account node_id_a)
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			node_id = node_id_a;
		}

		uint8_t get_network_version () const
		{
			return network_version;
		}

		void set_network_version (uint8_t network_version_a)
		{
			network_version = network_version_a;
		}

		mutable vban::mutex channel_mutex;

	private:
		std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::now () };
		std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::now () };
		boost::optional<vban::account> node_id{ boost::none };
		std::atomic<uint8_t> network_version{ 0 };

	protected:
		vban::node & node;
	};

	class channel_loopback final : public vban::transport::channel
	{
	public:
		channel_loopback (vban::node &);
		size_t hash_code () const override;
		bool operator== (vban::transport::channel const &) const override;
		void send_buffer (vban::shared_const_buffer const &, std::function<void (boost::system::error_code const &, size_t)> const & = nullptr, vban::buffer_drop_policy = vban::buffer_drop_policy::limiter) override;
		std::string to_string () const override;
		bool operator== (vban::transport::channel_loopback const & other_a) const
		{
			return endpoint == other_a.get_endpoint ();
		}

		vban::endpoint get_endpoint () const override
		{
			return endpoint;
		}

		vban::tcp_endpoint get_tcp_endpoint () const override
		{
			return vban::transport::map_endpoint_to_tcp (endpoint);
		}

		vban::transport::transport_type get_type () const override
		{
			return vban::transport::transport_type::loopback;
		}

	private:
		vban::endpoint const endpoint;
	};
} // namespace transport
} // namespace vban

namespace std
{
template <>
struct hash<::vban::transport::channel>
{
	size_t operator() (::vban::transport::channel const & channel_a) const
	{
		return channel_a.hash_code ();
	}
};
template <>
struct equal_to<std::reference_wrapper<::vban::transport::channel const>>
{
	bool operator() (std::reference_wrapper<::vban::transport::channel const> const & lhs, std::reference_wrapper<::vban::transport::channel const> const & rhs) const
	{
		return lhs.get () == rhs.get ();
	}
};
}

namespace boost
{
template <>
struct hash<::vban::transport::channel>
{
	size_t operator() (::vban::transport::channel const & channel_a) const
	{
		std::hash<::vban::transport::channel> hash;
		return hash (channel_a);
	}
};
template <>
struct hash<std::reference_wrapper<::vban::transport::channel const>>
{
	size_t operator() (std::reference_wrapper<::vban::transport::channel const> const & channel_a) const
	{
		std::hash<::vban::transport::channel> hash;
		return hash (channel_a.get ());
	}
};
}
