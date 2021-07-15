#pragma once

#include <vban/lib/numbers.hpp>
#include <vban/lib/utility.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace vban
{
class block_store;
class transaction;

class rep_weights
{
public:
	void representation_add (vban::account const & source_rep_a, vban::uint256_t const & amount_a);
	void representation_add_dual (vban::account const & source_rep_1, vban::uint256_t const & amount_1, vban::account const & source_rep_2, vban::uint256_t const & amount_2);
	vban::uint256_t representation_get (vban::account const & account_a) const;
	void representation_put (vban::account const & account_a, vban::uint128_union const & representation_a);
	std::unordered_map<vban::account, vban::uint256_t> get_rep_amounts () const;
	void copy_from (rep_weights & other_a);

private:
	mutable vban::mutex mutex;
	std::unordered_map<vban::account, vban::uint256_t> rep_amounts;
	void put (vban::account const & account_a, vban::uint128_union const & representation_a);
	vban::uint256_t get (vban::account const & account_a) const;

	friend std::unique_ptr<container_info_component> collect_container_info (rep_weights const &, const std::string &);
};

std::unique_ptr<container_info_component> collect_container_info (rep_weights const &, const std::string &);
}
