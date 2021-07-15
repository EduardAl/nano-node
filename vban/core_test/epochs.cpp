#include <vban/lib/epoch.hpp>
#include <vban/secure/common.hpp>

#include <gtest/gtest.h>

TEST (epochs, is_epoch_link)
{
	vban::epochs epochs;
	// Test epoch 1
	vban::keypair key1;
	auto link1 = 42;
	auto link2 = 43;
	ASSERT_FALSE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	epochs.add (vban::epoch::epoch_1, key1.pub, link1);
	ASSERT_TRUE (epochs.is_epoch_link (link1));
	ASSERT_FALSE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key1.pub, epochs.signer (vban::epoch::epoch_1));
	ASSERT_EQ (epochs.epoch (link1), vban::epoch::epoch_1);

	// Test epoch 2
	vban::keypair key2;
	epochs.add (vban::epoch::epoch_2, key2.pub, link2);
	ASSERT_TRUE (epochs.is_epoch_link (link2));
	ASSERT_EQ (key2.pub, epochs.signer (vban::epoch::epoch_2));
	ASSERT_EQ (vban::uint256_union (link1), epochs.link (vban::epoch::epoch_1));
	ASSERT_EQ (vban::uint256_union (link2), epochs.link (vban::epoch::epoch_2));
	ASSERT_EQ (epochs.epoch (link2), vban::epoch::epoch_2);
}

TEST (epochs, is_sequential)
{
	ASSERT_TRUE (vban::epochs::is_sequential (vban::epoch::epoch_0, vban::epoch::epoch_1));
	ASSERT_TRUE (vban::epochs::is_sequential (vban::epoch::epoch_1, vban::epoch::epoch_2));

	ASSERT_FALSE (vban::epochs::is_sequential (vban::epoch::epoch_0, vban::epoch::epoch_2));
	ASSERT_FALSE (vban::epochs::is_sequential (vban::epoch::epoch_0, vban::epoch::invalid));
	ASSERT_FALSE (vban::epochs::is_sequential (vban::epoch::unspecified, vban::epoch::epoch_1));
	ASSERT_FALSE (vban::epochs::is_sequential (vban::epoch::epoch_1, vban::epoch::epoch_0));
	ASSERT_FALSE (vban::epochs::is_sequential (vban::epoch::epoch_2, vban::epoch::epoch_0));
	ASSERT_FALSE (vban::epochs::is_sequential (vban::epoch::epoch_2, vban::epoch::epoch_2));
}
