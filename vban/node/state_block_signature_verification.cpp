#include <vban/lib/logger_mt.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/lib/threading.hpp>
#include <vban/lib/timer.hpp>
#include <vban/node/nodeconfig.hpp>
#include <vban/node/signatures.hpp>
#include <vban/node/state_block_signature_verification.hpp>
#include <vban/secure/common.hpp>

#include <boost/format.hpp>

vban::state_block_signature_verification::state_block_signature_verification (vban::signature_checker & signature_checker, vban::epochs & epochs, vban::node_config & node_config, vban::logger_mt & logger, uint64_t state_block_signature_verification_size) :
	signature_checker (signature_checker),
	epochs (epochs),
	node_config (node_config),
	logger (logger),
	thread ([this, state_block_signature_verification_size] () {
		vban::thread_role::set (vban::thread_role::name::state_block_signature_verification);
		this->run (state_block_signature_verification_size);
	})
{
}

vban::state_block_signature_verification::~state_block_signature_verification ()
{
	stop ();
}

void vban::state_block_signature_verification::stop ()
{
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		stopped = true;
	}

	if (thread.joinable ())
	{
		condition.notify_one ();
		thread.join ();
	}
}

void vban::state_block_signature_verification::run (uint64_t state_block_signature_verification_size)
{
	vban::unique_lock<vban::mutex> lk (mutex);
	while (!stopped)
	{
		if (!state_blocks.empty ())
		{
			size_t const max_verification_batch (state_block_signature_verification_size != 0 ? state_block_signature_verification_size : vban::signature_checker::batch_size * (node_config.signature_checker_threads + 1));
			active = true;
			while (!state_blocks.empty () && !stopped)
			{
				auto items = setup_items (max_verification_batch);
				lk.unlock ();
				verify_state_blocks (items);
				lk.lock ();
			}
			active = false;
			lk.unlock ();
			transition_inactive_callback ();
			lk.lock ();
		}
		else
		{
			condition.wait (lk);
		}
	}
}

bool vban::state_block_signature_verification::is_active ()
{
	vban::lock_guard<vban::mutex> guard (mutex);
	return active;
}

void vban::state_block_signature_verification::add (vban::unchecked_info const & info_a)
{
	{
		vban::lock_guard<vban::mutex> guard (mutex);
		state_blocks.emplace_back (info_a);
	}
	condition.notify_one ();
}

size_t vban::state_block_signature_verification::size ()
{
	vban::lock_guard<vban::mutex> guard (mutex);
	return state_blocks.size ();
}

std::deque<vban::unchecked_info> vban::state_block_signature_verification::setup_items (size_t max_count)
{
	std::deque<vban::unchecked_info> items;
	if (state_blocks.size () <= max_count)
	{
		items.swap (state_blocks);
	}
	else
	{
		for (auto i (0); i < max_count; ++i)
		{
			items.push_back (state_blocks.front ());
			state_blocks.pop_front ();
		}
		debug_assert (!state_blocks.empty ());
	}
	return items;
}

void vban::state_block_signature_verification::verify_state_blocks (std::deque<vban::unchecked_info> & items)
{
	if (!items.empty ())
	{
		vban::timer<> timer_l;
		timer_l.start ();
		auto size (items.size ());
		std::vector<vban::block_hash> hashes;
		hashes.reserve (size);
		std::vector<unsigned char const *> messages;
		messages.reserve (size);
		std::vector<size_t> lengths;
		lengths.reserve (size);
		std::vector<vban::account> accounts;
		accounts.reserve (size);
		std::vector<unsigned char const *> pub_keys;
		pub_keys.reserve (size);
		std::vector<vban::signature> blocks_signatures;
		blocks_signatures.reserve (size);
		std::vector<unsigned char const *> signatures;
		signatures.reserve (size);
		std::vector<int> verifications;
		verifications.resize (size, 0);
		for (auto & item : items)
		{
			hashes.push_back (item.block->hash ());
			messages.push_back (hashes.back ().bytes.data ());
			lengths.push_back (sizeof (decltype (hashes)::value_type));
			vban::account account (item.block->account ());
			if (!item.block->link ().is_zero () && epochs.is_epoch_link (item.block->link ()))
			{
				account = epochs.signer (epochs.epoch (item.block->link ()));
			}
			else if (!item.account.is_zero ())
			{
				account = item.account;
			}
			accounts.push_back (account);
			pub_keys.push_back (accounts.back ().bytes.data ());
			blocks_signatures.push_back (item.block->block_signature ());
			signatures.push_back (blocks_signatures.back ().bytes.data ());
		}
		vban::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data () };
		signature_checker.verify (check);
		if (node_config.logging.timing_logging () && timer_l.stop () > std::chrono::milliseconds (10))
		{
			logger.try_log (boost::str (boost::format ("Batch verified %1% state blocks in %2% %3%") % size % timer_l.value ().count () % timer_l.unit ()));
		}
		blocks_verified_callback (items, verifications, hashes, blocks_signatures);
	}
}

std::unique_ptr<vban::container_info_component> vban::collect_container_info (state_block_signature_verification & state_block_signature_verification, std::string const & name)
{
	auto composite = std::make_unique<container_info_composite> (name);
	composite->add_component (std::make_unique<container_info_leaf> (container_info{ "state_blocks", state_block_signature_verification.size (), sizeof (vban::unchecked_info) }));
	return composite;
}
