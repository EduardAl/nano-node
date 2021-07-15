#include <vban/lib/blocks.hpp>
#include <vban/lib/numbers.hpp>
#include <vban/node/ipc/flatbuffers_util.hpp>
#include <vban/secure/common.hpp>

std::unique_ptr<vbanapi::BlockStateT> vban::ipc::flatbuffers_builder::from (vban::state_block const & block_a, vban::amount const & amount_a, bool is_state_send_a)
{
	static vban::network_params params;
	auto block (std::make_unique<vbanapi::BlockStateT> ());
	block->account = block_a.account ().to_account ();
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative ().to_account ();
	block->balance = block_a.balance ().to_string_dec ();
	block->link = block_a.link ().to_string ();
	block->link_as_account = block_a.link ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = vban::to_string_hex (block_a.work);

	if (is_state_send_a)
	{
		block->subtype = vbanapi::BlockSubType::BlockSubType_send;
	}
	else if (block_a.link ().is_zero ())
	{
		block->subtype = vbanapi::BlockSubType::BlockSubType_change;
	}
	else if (amount_a == 0 && params.ledger.epochs.is_epoch_link (block_a.link ()))
	{
		block->subtype = vbanapi::BlockSubType::BlockSubType_epoch;
	}
	else
	{
		block->subtype = vbanapi::BlockSubType::BlockSubType_receive;
	}
	return block;
}

std::unique_ptr<vbanapi::BlockSendT> vban::ipc::flatbuffers_builder::from (vban::send_block const & block_a)
{
	auto block (std::make_unique<vbanapi::BlockSendT> ());
	block->hash = block_a.hash ().to_string ();
	block->balance = block_a.balance ().to_string_dec ();
	block->destination = block_a.hashables.destination.to_account ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = vban::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<vbanapi::BlockReceiveT> vban::ipc::flatbuffers_builder::from (vban::receive_block const & block_a)
{
	auto block (std::make_unique<vbanapi::BlockReceiveT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block_a.signature.encode_hex (block->signature);
	block->work = vban::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<vbanapi::BlockOpenT> vban::ipc::flatbuffers_builder::from (vban::open_block const & block_a)
{
	auto block (std::make_unique<vbanapi::BlockOpenT> ());
	block->hash = block_a.hash ().to_string ();
	block->source = block_a.source ().to_string ();
	block->account = block_a.account ().to_account ();
	block->representative = block_a.representative ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = vban::to_string_hex (block_a.work);
	return block;
}

std::unique_ptr<vbanapi::BlockChangeT> vban::ipc::flatbuffers_builder::from (vban::change_block const & block_a)
{
	auto block (std::make_unique<vbanapi::BlockChangeT> ());
	block->hash = block_a.hash ().to_string ();
	block->previous = block_a.previous ().to_string ();
	block->representative = block_a.representative ().to_account ();
	block_a.signature.encode_hex (block->signature);
	block->work = vban::to_string_hex (block_a.work);
	return block;
}

vbanapi::BlockUnion vban::ipc::flatbuffers_builder::block_to_union (vban::block const & block_a, vban::amount const & amount_a, bool is_state_send_a)
{
	vbanapi::BlockUnion u;
	switch (block_a.type ())
	{
		case vban::block_type::state:
		{
			u.Set (*from (dynamic_cast<vban::state_block const &> (block_a), amount_a, is_state_send_a));
			break;
		}
		case vban::block_type::send:
		{
			u.Set (*from (dynamic_cast<vban::send_block const &> (block_a)));
			break;
		}
		case vban::block_type::receive:
		{
			u.Set (*from (dynamic_cast<vban::receive_block const &> (block_a)));
			break;
		}
		case vban::block_type::open:
		{
			u.Set (*from (dynamic_cast<vban::open_block const &> (block_a)));
			break;
		}
		case vban::block_type::change:
		{
			u.Set (*from (dynamic_cast<vban::change_block const &> (block_a)));
			break;
		}

		default:
			debug_assert (false);
	}
	return u;
}
