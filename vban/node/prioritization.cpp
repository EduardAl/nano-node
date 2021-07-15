#include <vban/lib/blocks.hpp>
#include <vban/lib/utility.hpp>
#include <vban/node/prioritization.hpp>

#include <string>

bool vban::prioritization::value_type::operator< (value_type const & other_a) const
{
	return time < other_a.time || (time == other_a.time && block->hash () < other_a.block->hash ());
}

bool vban::prioritization::value_type::operator== (value_type const & other_a) const
{
	return time == other_a.time && block->hash () == other_a.block->hash ();
}

void vban::prioritization::next ()
{
	++current;
	if (current == schedule.end ())
	{
		current = schedule.begin ();
	}
}

void vban::prioritization::seek ()
{
	next ();
	for (size_t i = 0, n = schedule.size (); buckets[*current].empty () && i < n; ++i)
	{
		next ();
	}
}

void vban::prioritization::populate_schedule ()
{
	for (auto i = 0; i < buckets.size (); ++i)
	{
		schedule.push_back (i);
	}
}

vban::prioritization::prioritization (uint64_t maximum, std::function<void (std::shared_ptr<vban::block>)> const & drop_a) :
	drop{ drop_a },
	maximum{ maximum }
{
	static size_t constexpr bucket_count = 129;
	buckets.resize (bucket_count);
	vban::uint256_t minimum{ 1 };
	minimums.push_back (0);
	for (auto i = 1; i < bucket_count; ++i)
	{
		minimums.push_back (minimum);
		minimum <<= 1;
	}
	populate_schedule ();
	current = schedule.begin ();
}

void vban::prioritization::push (uint64_t time, std::shared_ptr<vban::block> block)
{
	auto was_empty = empty ();
	auto block_has_balance = block->type () == vban::block_type::state || block->type () == vban::block_type::send;
	debug_assert (block_has_balance || block->has_sideband ());
	auto balance = block_has_balance ? block->balance () : block->sideband ().balance;
	auto index = std::upper_bound (minimums.begin (), minimums.end (), balance.number ()) - 1 - minimums.begin ();
	auto & bucket = buckets[index];
	bucket.emplace (value_type{ time, block });
	if (bucket.size () > std::max (decltype (maximum){ 1 }, maximum / buckets.size ()))
	{
		bucket.erase (--bucket.end ());
	}
	if (was_empty)
	{
		seek ();
	}
}

std::shared_ptr<vban::block> vban::prioritization::top () const
{
	debug_assert (!empty ());
	debug_assert (!buckets[*current].empty ());
	auto result = buckets[*current].begin ()->block;
	return result;
}

void vban::prioritization::pop ()
{
	debug_assert (!empty ());
	debug_assert (!buckets[*current].empty ());
	auto & bucket = buckets[*current];
	bucket.erase (bucket.begin ());
	seek ();
}

size_t vban::prioritization::size () const
{
	size_t result{ 0 };
	for (auto const & queue : buckets)
	{
		result += queue.size ();
	}
	return result;
}

size_t vban::prioritization::bucket_count () const
{
	return buckets.size ();
}

size_t vban::prioritization::bucket_size (size_t index) const
{
	return buckets[index].size ();
}

bool vban::prioritization::empty () const
{
	return std::all_of (buckets.begin (), buckets.end (), [] (priority const & bucket_a) { return bucket_a.empty (); });
}

void vban::prioritization::dump ()
{
	for (auto const & i : buckets)
	{
		for (auto const & j : i)
		{
			std::cerr << j.time << ' ' << j.block->hash ().to_string () << '\n';
		}
	}
	std::cerr << "current: " << std::to_string (*current) << '\n';
}
