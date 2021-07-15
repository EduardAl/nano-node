#include <vban/lib/memory.hpp>
#include <vban/node/active_transactions.hpp>
#include <vban/secure/common.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
/** This allocator records the size of all allocations that happen */
template <class T>
class record_allocations_new_delete_allocator
{
public:
	using value_type = T;

	explicit record_allocations_new_delete_allocator (std::vector<size_t> * allocated) :
		allocated (allocated)
	{
	}

	template <typename U>
	record_allocations_new_delete_allocator (const record_allocations_new_delete_allocator<U> & a)
	{
		allocated = a.allocated;
	}

	template <typename U>
	record_allocations_new_delete_allocator & operator= (const record_allocations_new_delete_allocator<U> &) = delete;

	T * allocate (size_t num_to_allocate)
	{
		auto size_allocated = (sizeof (T) * num_to_allocate);
		allocated->push_back (size_allocated);
		return static_cast<T *> (::operator new (size_allocated));
	}

	void deallocate (T * p, size_t num_to_deallocate)
	{
		::operator delete (p);
	}

	std::vector<size_t> * allocated;
};

template <typename T>
size_t get_allocated_size ()
{
	std::vector<size_t> allocated;
	record_allocations_new_delete_allocator<T> alloc (&allocated);
	(void)std::allocate_shared<T, record_allocations_new_delete_allocator<T>> (alloc);
	debug_assert (allocated.size () == 1);
	return allocated.front ();
}
}

TEST (memory_pool, validate_cleanup)
{
	// This might be turned off, e.g on Mac for instance, so don't do this test
	if (!vban::get_use_memory_pools ())
	{
		return;
	}

	vban::make_shared<vban::open_block> ();
	vban::make_shared<vban::receive_block> ();
	vban::make_shared<vban::send_block> ();
	vban::make_shared<vban::change_block> ();
	vban::make_shared<vban::state_block> ();
	vban::make_shared<vban::vote> ();

	ASSERT_TRUE (vban::purge_shared_ptr_singleton_pool_memory<vban::open_block> ());
	ASSERT_TRUE (vban::purge_shared_ptr_singleton_pool_memory<vban::receive_block> ());
	ASSERT_TRUE (vban::purge_shared_ptr_singleton_pool_memory<vban::send_block> ());
	ASSERT_TRUE (vban::purge_shared_ptr_singleton_pool_memory<vban::state_block> ());
	ASSERT_TRUE (vban::purge_shared_ptr_singleton_pool_memory<vban::vote> ());

	// Change blocks have the same size as open_block so won't deallocate any memory
	ASSERT_FALSE (vban::purge_shared_ptr_singleton_pool_memory<vban::change_block> ());

	ASSERT_EQ (vban::determine_shared_ptr_pool_size<vban::open_block> (), get_allocated_size<vban::open_block> () - sizeof (size_t));
	ASSERT_EQ (vban::determine_shared_ptr_pool_size<vban::receive_block> (), get_allocated_size<vban::receive_block> () - sizeof (size_t));
	ASSERT_EQ (vban::determine_shared_ptr_pool_size<vban::send_block> (), get_allocated_size<vban::send_block> () - sizeof (size_t));
	ASSERT_EQ (vban::determine_shared_ptr_pool_size<vban::change_block> (), get_allocated_size<vban::change_block> () - sizeof (size_t));
	ASSERT_EQ (vban::determine_shared_ptr_pool_size<vban::state_block> (), get_allocated_size<vban::state_block> () - sizeof (size_t));
	ASSERT_EQ (vban::determine_shared_ptr_pool_size<vban::vote> (), get_allocated_size<vban::vote> () - sizeof (size_t));

	{
		vban::active_transactions::ordered_cache inactive_votes_cache;
		vban::account representative{ 1 };
		vban::block_hash hash{ 1 };
		uint64_t timestamp{ 1 };
		vban::inactive_cache_status default_status{};
		inactive_votes_cache.emplace (std::chrono::steady_clock::now (), hash, representative, timestamp, default_status);
	}

	ASSERT_TRUE (vban::purge_singleton_inactive_votes_cache_pool_memory ());
}
