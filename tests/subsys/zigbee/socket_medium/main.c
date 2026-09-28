/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "native_sim_socket_medium.h"

#include <zephyr/ztest.h>






ZTEST(socket_medium, test_round_trip_tx_message)
{
	static const uint8_t psdu[] = {
		0x63, 0x88, 0x4a, 0x27, 0x5b, 0x00, 0x00, 0x02,
		0x00, 0x50, 0xe0, 0x38, 0xc1, 0xa4, 0x04,
	};
	uint8_t buffer[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	struct zb_native_sim_socket_medium_msg msg;
	struct zb_native_sim_socket_medium_msg parsed;
	size_t encoded_len = 0U;
	int rc;

	memset(&msg, 0, sizeof(msg));
	msg.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX;
	msg.node_id = 0x2202U;
	msg.channel = 11U;
	msg.tx_power_dbm = 3;
	msg.psdu = psdu;
	msg.psdu_len = sizeof(psdu);

	rc = zb_native_sim_socket_medium_encode(buffer, sizeof(buffer), &msg, &encoded_len);
	zassert_equal(rc, 0);
	zassert_true(encoded_len > sizeof(psdu));

	memset(&parsed, 0, sizeof(parsed));
	rc = zb_native_sim_socket_medium_decode(&parsed, buffer, encoded_len);
	zassert_equal(rc, 0);
	zassert_equal(parsed.type, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX);
	zassert_equal(parsed.node_id, 0x2202U);
	zassert_equal(parsed.channel, 11U);
	zassert_equal(parsed.tx_power_dbm, 3);
	zassert_equal(parsed.psdu_len, sizeof(psdu));
	zassert_mem_equal(parsed.psdu, psdu, sizeof(psdu));
}

ZTEST(socket_medium, test_filter_update_controls_medium_state)
{
	static const uint8_t ieee_addr[] = { 0x02, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4 };
	struct zb_native_sim_socket_medium_peer peer;
	struct zb_native_sim_socket_medium_msg msg;
	int rc;

	zb_native_sim_socket_medium_peer_reset(&peer);

	memset(&msg, 0, sizeof(msg));
	msg.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER;
	msg.node_id = 0x2202U;
	msg.channel = 11U;
	msg.rx_on = true;
	msg.pan_id = 0x5b27U;
	msg.short_addr = 0x2700U;
	memcpy(msg.ieee_addr, ieee_addr, sizeof(msg.ieee_addr));

	rc = zb_native_sim_socket_medium_peer_apply(&peer, &msg);
	zassert_equal(rc, 0);
	zassert_equal(peer.node_id, 0x2202U);
	zassert_equal(peer.channel, 11U);
	zassert_true(peer.rx_on);
	zassert_equal(peer.pan_id, 0x5b27U);
	zassert_equal(peer.short_addr, 0x2700U);
	zassert_mem_equal(peer.ieee_addr, ieee_addr, sizeof(ieee_addr));
}

ZTEST(socket_medium, test_filter_matching_uses_pan_and_short_address)
{
	static const uint8_t accepted_psdu[] = {
		0x61, 0x88, 0x01, 0x27, 0x5b, 0x00, 0x27, 0x00,
		0x00, 0x00, 0x34, 0x12, 0x99, 0x88,
	};
	static const uint8_t rejected_pan_psdu[] = {
		0x61, 0x88, 0x01, 0x28, 0x5b, 0x00, 0x27, 0x00,
		0x00, 0x00, 0x34, 0x12, 0x99, 0x88,
	};
	static const uint8_t rejected_short_psdu[] = {
		0x61, 0x88, 0x01, 0x27, 0x5b, 0x33, 0x27, 0x00,
		0x00, 0x00, 0x34, 0x12, 0x99, 0x88,
	};
	struct zb_native_sim_socket_medium_peer peer;

	zb_native_sim_socket_medium_peer_reset(&peer);
	peer.rx_on = true;
	peer.channel = 11U;
	peer.pan_id = 0x5b27U;
	peer.short_addr = 0x2700U;

	zassert_true(zb_native_sim_socket_medium_peer_accepts_psdu(&peer, accepted_psdu,
								 sizeof(accepted_psdu)));
	zassert_false(zb_native_sim_socket_medium_peer_accepts_psdu(&peer, rejected_pan_psdu,
								  sizeof(rejected_pan_psdu)));
	zassert_false(zb_native_sim_socket_medium_peer_accepts_psdu(&peer,
								  rejected_short_psdu,
								  sizeof(rejected_short_psdu)));
}

ZTEST_SUITE(socket_medium, NULL, NULL, NULL, NULL, NULL);
