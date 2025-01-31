#include <vban/node/common.hpp>
#include <vban/test_common/telemetry.hpp>

#include <gtest/gtest.h>

void vban::compare_default_telemetry_response_data_excluding_signature (vban::telemetry_data const & telemetry_data_a, vban::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a)
{
	ASSERT_EQ (telemetry_data_a.block_count, 1);
	ASSERT_EQ (telemetry_data_a.cemented_count, 1);
	ASSERT_EQ (telemetry_data_a.bandwidth_cap, bandwidth_limit_a);
	ASSERT_EQ (telemetry_data_a.peer_count, 1);
	ASSERT_EQ (telemetry_data_a.protocol_version, network_params_a.protocol.protocol_version);
	ASSERT_EQ (telemetry_data_a.unchecked_count, 0);
	ASSERT_EQ (telemetry_data_a.account_count, 1);
	ASSERT_LT (telemetry_data_a.uptime, 100);
	ASSERT_EQ (telemetry_data_a.genesis_block, network_params_a.ledger.genesis_hash);
	ASSERT_EQ (telemetry_data_a.major_version, vban::get_major_node_version ());
	ASSERT_EQ (telemetry_data_a.minor_version, vban::get_minor_node_version ());
	ASSERT_EQ (telemetry_data_a.patch_version, vban::get_patch_node_version ());
	ASSERT_EQ (telemetry_data_a.pre_release_version, vban::get_pre_release_node_version ());
	ASSERT_EQ (telemetry_data_a.maker, static_cast<std::underlying_type_t<vban::telemetry_maker>> (vban::telemetry_maker::nf_node));
	ASSERT_GT (telemetry_data_a.timestamp, std::chrono::system_clock::now () - std::chrono::seconds (100));
	ASSERT_EQ (telemetry_data_a.active_difficulty, active_difficulty_a);
	ASSERT_EQ (telemetry_data_a.unknown_data, std::vector<uint8_t>{});
}

void vban::compare_default_telemetry_response_data (vban::telemetry_data const & telemetry_data_a, vban::network_params const & network_params_a, uint64_t bandwidth_limit_a, uint64_t active_difficulty_a, vban::keypair const & node_id_a)
{
	ASSERT_FALSE (telemetry_data_a.validate_signature ());
	vban::telemetry_data telemetry_data_l = telemetry_data_a;
	telemetry_data_l.signature.clear ();
	telemetry_data_l.sign (node_id_a);
	// Signature should be different because uptime/timestamp will have changed.
	ASSERT_NE (telemetry_data_a.signature, telemetry_data_l.signature);
	compare_default_telemetry_response_data_excluding_signature (telemetry_data_a, network_params_a, bandwidth_limit_a, active_difficulty_a);
	ASSERT_EQ (telemetry_data_a.node_id, node_id_a.pub);
}
