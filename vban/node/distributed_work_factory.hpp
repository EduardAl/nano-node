#pragma once

#include <vban/lib/numbers.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace vban
{
class container_info_component;
class distributed_work;
class node;
class root;
struct work_request;

class distributed_work_factory final
{
public:
	distributed_work_factory (vban::node &);
	~distributed_work_factory ();
	bool make (vban::work_version const, vban::root const &, std::vector<std::pair<std::string, uint16_t>> const &, uint64_t, std::function<void (boost::optional<uint64_t>)> const &, boost::optional<vban::account> const & = boost::none);
	bool make (std::chrono::seconds const &, vban::work_request const &);
	void cancel (vban::root const &);
	void cleanup_finished ();
	void stop ();
	size_t size () const;

private:
	std::unordered_multimap<vban::root, std::weak_ptr<vban::distributed_work>> items;

	vban::node & node;
	mutable vban::mutex mutex;
	std::atomic<bool> stopped{ false };

	friend std::unique_ptr<container_info_component> collect_container_info (distributed_work_factory &, const std::string &);
};

std::unique_ptr<container_info_component> collect_container_info (distributed_work_factory & distributed_work, std::string const & name);
}
