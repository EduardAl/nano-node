#include <vban/crypto_lib/random_pool.hpp>
#include <vban/lib/locks.hpp>
#include <vban/secure/buffer.hpp>
#include <vban/secure/common.hpp>
#include <vban/secure/network_filter.hpp>

vban::network_filter::network_filter (size_t size_a) :
	items (size_a, vban::uint256_t{ 0 })
{
	vban::random_pool::generate_block (key, key.size ());
}

bool vban::network_filter::apply (uint8_t const * bytes_a, size_t count_a, vban::uint256_t * digest_a)
{
	// Get hash before locking
	auto digest (hash (bytes_a, count_a));

	vban::lock_guard<vban::mutex> lock (mutex);
	auto & element (get_element (digest));
	bool existed (element == digest);
	if (!existed)
	{
		// Replace likely old element with a new one
		element = digest;
	}
	if (digest_a)
	{
		*digest_a = digest;
	}
	return existed;
}

void vban::network_filter::clear (vban::uint256_t const & digest_a)
{
	vban::lock_guard<vban::mutex> lock (mutex);
	auto & element (get_element (digest_a));
	if (element == digest_a)
	{
		element = vban::uint256_t{ 0 };
	}
}

void vban::network_filter::clear (std::vector<vban::uint256_t> const & digests_a)
{
	vban::lock_guard<vban::mutex> lock (mutex);
	for (auto const & digest : digests_a)
	{
		auto & element (get_element (digest));
		if (element == digest)
		{
			element = vban::uint256_t{ 0 };
		}
	}
}

void vban::network_filter::clear (uint8_t const * bytes_a, size_t count_a)
{
	clear (hash (bytes_a, count_a));
}

template <typename OBJECT>
void vban::network_filter::clear (OBJECT const & object_a)
{
	clear (hash (object_a));
}

void vban::network_filter::clear ()
{
	vban::lock_guard<vban::mutex> lock (mutex);
	items.assign (items.size (), vban::uint256_t{ 0 });
}

template <typename OBJECT>
vban::uint256_t vban::network_filter::hash (OBJECT const & object_a) const
{
	std::vector<uint8_t> bytes;
	{
		vban::vectorstream stream (bytes);
		object_a->serialize (stream);
	}
	return hash (bytes.data (), bytes.size ());
}

vban::uint256_t & vban::network_filter::get_element (vban::uint256_t const & hash_a)
{
	debug_assert (!mutex.try_lock ());
	debug_assert (items.size () > 0);
	size_t index (hash_a % items.size ());
	return items[index];
}

vban::uint256_t vban::network_filter::hash (uint8_t const * bytes_a, size_t count_a) const
{
	vban::uint128_union digest{ 0 };
	siphash_t siphash (key, static_cast<unsigned int> (key.size ()));
	siphash.CalculateDigest (digest.bytes.data (), bytes_a, count_a);
	return digest.number ();
}

// Explicitly instantiate
template vban::uint256_t vban::network_filter::hash (std::shared_ptr<vban::block> const &) const;
template void vban::network_filter::clear (std::shared_ptr<vban::block> const &);
