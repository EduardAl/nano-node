#pragma once

#include <vban/node/common.hpp>
#include <vban/node/transport/transport.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

#include <mutex>
#include <unordered_set>

namespace mi = boost::multi_index;

namespace vban
{
class message_buffer;
namespace transport
{
	class udp_channels;
	class channel_udp final : public vban::transport::channel
	{
		friend class vban::transport::udp_channels;

	public:
		channel_udp (vban::transport::udp_channels &, vban::endpoint const &, uint8_t protocol_version);
		size_t hash_code () const override;
		bool operator== (vban::transport::channel const &) const override;
		void send_buffer (vban::shared_const_buffer const &, std::function<void (boost::system::error_code const &, size_t)> const & = nullptr, vban::buffer_drop_policy = vban::buffer_drop_policy::limiter) override;
		std::string to_string () const override;
		bool operator== (vban::transport::channel_udp const & other_a) const
		{
			return &channels == &other_a.channels && endpoint == other_a.endpoint;
		}

		vban::endpoint get_endpoint () const override
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			return endpoint;
		}

		vban::tcp_endpoint get_tcp_endpoint () const override
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			return vban::transport::map_endpoint_to_tcp (endpoint);
		}

		vban::transport::transport_type get_type () const override
		{
			return vban::transport::transport_type::udp;
		}

		std::chrono::steady_clock::time_point get_last_telemetry_req ()
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			return last_telemetry_req;
		}

		void set_last_telemetry_req (std::chrono::steady_clock::time_point const time_a)
		{
			vban::lock_guard<vban::mutex> lk (channel_mutex);
			last_telemetry_req = time_a;
		}

	private:
		vban::endpoint endpoint;
		vban::transport::udp_channels & channels;
		std::chrono::steady_clock::time_point last_telemetry_req{ std::chrono::steady_clock::time_point () };
	};
	class udp_channels final
	{
		friend class vban::transport::channel_udp;

	public:
		udp_channels (vban::node &, uint16_t);
		std::shared_ptr<vban::transport::channel_udp> insert (vban::endpoint const &, unsigned);
		void erase (vban::endpoint const &);
		size_t size () const;
		std::shared_ptr<vban::transport::channel_udp> channel (vban::endpoint const &) const;
		void random_fill (std::array<vban::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<vban::transport::channel>> random_set (size_t, uint8_t = 0) const;
		bool store_all (bool = true);
		std::shared_ptr<vban::transport::channel_udp> find_node_id (vban::account const &);
		void clean_node_id (vban::account const &);
		void clean_node_id (vban::endpoint const &, vban::account const &);
		// Get the next peer for attempting a tcp bootstrap connection
		vban::tcp_endpoint bootstrap_peer (uint8_t connection_protocol_version_min);
		void receive ();
		void start ();
		void stop ();
		void send (vban::shared_const_buffer const & buffer_a, vban::endpoint endpoint_a, std::function<void (boost::system::error_code const &, size_t)> const & callback_a);
		vban::endpoint get_local_endpoint () const;
		void receive_action (vban::message_buffer *);
		void process_packets ();
		std::shared_ptr<vban::transport::channel> create (vban::endpoint const &);
		bool max_ip_connections (vban::endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (vban::endpoint const &);
		std::unique_ptr<container_info_component> collect_container_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void ongoing_keepalive ();
		void list_below_version (std::vector<std::shared_ptr<vban::transport::channel>> &, uint8_t);
		void list (std::deque<std::shared_ptr<vban::transport::channel>> &, uint8_t = 0);
		void modify (std::shared_ptr<vban::transport::channel_udp> const &, std::function<void (std::shared_ptr<vban::transport::channel_udp> const &)>);
		vban::node & node;

	private:
		void close_socket ();
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class subnetwork_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_received_tag
		{
		};
		class last_bootstrap_attempt_tag
		{
		};
		class last_attempt_tag
		{
		};
		class node_id_tag
		{
		};
		class channel_udp_wrapper final
		{
		public:
			std::shared_ptr<vban::transport::channel_udp> channel;
			channel_udp_wrapper (std::shared_ptr<vban::transport::channel_udp> const & channel_a) :
				channel (channel_a)
			{
			}
			vban::endpoint endpoint () const
			{
				return channel->get_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_received () const
			{
				return channel->get_last_packet_received ();
			}
			std::chrono::steady_clock::time_point last_bootstrap_attempt () const
			{
				return channel->get_last_bootstrap_attempt ();
			}
			std::chrono::steady_clock::time_point last_telemetry_req () const
			{
				return channel->get_last_telemetry_req ();
			}
			boost::asio::ip::address ip_address () const
			{
				return vban::transport::ipv4_address_or_ipv6_subnet (endpoint ().address ());
			}
			boost::asio::ip::address subnetwork () const
			{
				return vban::transport::map_address_to_subnetwork (endpoint ().address ());
			}
			vban::account node_id () const
			{
				return channel->get_node_id ();
			}
		};
		class endpoint_attempt final
		{
		public:
			vban::endpoint endpoint;
			boost::asio::ip::address subnetwork;
			std::chrono::steady_clock::time_point last_attempt{ std::chrono::steady_clock::now () };

			explicit endpoint_attempt (vban::endpoint const & endpoint_a) :
				endpoint (endpoint_a),
				subnetwork (vban::transport::map_address_to_subnetwork (endpoint_a.address ()))
			{
			}
		};
		mutable vban::mutex mutex;
		// clang-format off
		boost::multi_index_container<
		channel_udp_wrapper,
		mi::indexed_by<
			mi::random_access<mi::tag<random_access_tag>>,
			mi::ordered_non_unique<mi::tag<last_bootstrap_attempt_tag>,
				mi::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_bootstrap_attempt>>,
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::const_mem_fun<channel_udp_wrapper, vban::endpoint, &channel_udp_wrapper::endpoint>>,
			mi::hashed_non_unique<mi::tag<node_id_tag>,
				mi::const_mem_fun<channel_udp_wrapper, vban::account, &channel_udp_wrapper::node_id>>,
			mi::ordered_non_unique<mi::tag<last_packet_received_tag>,
				mi::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_packet_received>>,
			mi::hashed_non_unique<mi::tag<ip_address_tag>,
				mi::const_mem_fun<channel_udp_wrapper, boost::asio::ip::address, &channel_udp_wrapper::ip_address>>,
			mi::hashed_non_unique<mi::tag<subnetwork_tag>,
				mi::const_mem_fun<channel_udp_wrapper, boost::asio::ip::address, &channel_udp_wrapper::subnetwork>>>>
		channels;
		boost::multi_index_container<
		endpoint_attempt,
		mi::indexed_by<
			mi::hashed_unique<mi::tag<endpoint_tag>,
				mi::member<endpoint_attempt, vban::endpoint, &endpoint_attempt::endpoint>>,
			mi::hashed_non_unique<mi::tag<subnetwork_tag>,
				mi::member<endpoint_attempt, boost::asio::ip::address, &endpoint_attempt::subnetwork>>,
			mi::ordered_non_unique<mi::tag<last_attempt_tag>,
				mi::member<endpoint_attempt, std::chrono::steady_clock::time_point, &endpoint_attempt::last_attempt>>>>
		attempts;
		// clang-format on
		boost::asio::strand<boost::asio::io_context::executor_type> strand;
		std::unique_ptr<boost::asio::ip::udp::socket> socket;
		vban::endpoint local_endpoint;
		std::atomic<bool> stopped{ false };
	};
} // namespace transport
} // namespace vban
