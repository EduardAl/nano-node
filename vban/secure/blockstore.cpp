#include <vban/lib/threading.hpp>
#include <vban/secure/blockstore.hpp>

vban::representative_visitor::representative_visitor (vban::transaction const & transaction_a, vban::block_store & store_a) :
	transaction (transaction_a),
	store (store_a),
	result (0)
{
}

void vban::representative_visitor::compute (vban::block_hash const & hash_a)
{
	current = hash_a;
	while (result.is_zero ())
	{
		auto block (store.block_get (transaction, current));
		debug_assert (block != nullptr);
		block->visit (*this);
	}
}

void vban::representative_visitor::send_block (vban::send_block const & block_a)
{
	current = block_a.previous ();
}

void vban::representative_visitor::receive_block (vban::receive_block const & block_a)
{
	current = block_a.previous ();
}

void vban::representative_visitor::open_block (vban::open_block const & block_a)
{
	result = block_a.hash ();
}

void vban::representative_visitor::change_block (vban::change_block const & block_a)
{
	result = block_a.hash ();
}

void vban::representative_visitor::state_block (vban::state_block const & block_a)
{
	result = block_a.hash ();
}

vban::read_transaction::read_transaction (std::unique_ptr<vban::read_transaction_impl> read_transaction_impl) :
	impl (std::move (read_transaction_impl))
{
}

void * vban::read_transaction::get_handle () const
{
	return impl->get_handle ();
}

void vban::read_transaction::reset () const
{
	impl->reset ();
}

void vban::read_transaction::renew () const
{
	impl->renew ();
}

void vban::read_transaction::refresh () const
{
	reset ();
	renew ();
}

vban::write_transaction::write_transaction (std::unique_ptr<vban::write_transaction_impl> write_transaction_impl) :
	impl (std::move (write_transaction_impl))
{
	/*
	 * For IO threads, we do not want them to block on creating write transactions.
	 */
	debug_assert (vban::thread_role::get () != vban::thread_role::name::io);
}

void * vban::write_transaction::get_handle () const
{
	return impl->get_handle ();
}

void vban::write_transaction::commit ()
{
	impl->commit ();
}

void vban::write_transaction::renew ()
{
	impl->renew ();
}

void vban::write_transaction::refresh ()
{
	impl->commit ();
	impl->renew ();
}

bool vban::write_transaction::contains (vban::tables table_a) const
{
	return impl->contains (table_a);
}
