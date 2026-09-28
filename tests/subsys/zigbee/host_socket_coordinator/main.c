/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "native_sim_socket_medium.h"
#include "native_sim_socket_medium_model.h"

#include <zephyr/ztest.h>

#include "coord_logic.h"

static void send_filter(struct zb_host_socket_coord *coord)
{
	static const uint8_t ieee_addr[] = {0x02, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4};
	struct zb_native_sim_socket_medium_msg filter = {
		.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER,
		.node_id = 0x2202U,
		.pan_id = 0x5b27U,
		.short_addr = 0xffffU,
		.channel = 11U,
		.rx_on = true,
	};

	memcpy(filter.ieee_addr, ieee_addr, sizeof(filter.ieee_addr));
	zassert_equal(zb_host_socket_coord_process(coord, &filter, NULL), 0);
}

static struct zb_native_sim_socket_medium_msg make_native_assoc_req(void)
{
	static const uint8_t psdu[] = {
		0x63, 0xC8, 0x01, 0x27, 0x5B, 0x00, 0x00, 0x02, 0x00,
		0x02, 0x50, 0xE0, 0x38, 0xC1, 0xA4, 0x01, 0x80,
	};
	struct zb_native_sim_socket_medium_msg msg = {
		.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX,
		.node_id = 0x2202U,
		.channel = 11U,
		.psdu = psdu,
		.psdu_len = sizeof(psdu),
	};

	return msg;
}

static void expect_single_output(struct zb_host_socket_coord *coord,
				 const struct zb_native_sim_socket_medium_msg *input,
				 enum zb_host_socket_frame_type expected_type)
{
	struct zb_native_sim_socket_medium_msg output;
	enum zb_host_socket_frame_type actual_type;

	memset(&output, 0, sizeof(output));
	zassert_equal(zb_host_socket_coord_process(coord, input, &output), 1);
	zassert_equal(output.type, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX);
	actual_type = zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len);
	zassert_equal(actual_type, expected_type);
}

ZTEST(host_socket_coordinator, test_join_and_interview_flow)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	char model_id[32];

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	/*
	 * The association response is an indirect transmission, as it is on air:
	 * the request queues it and the joiner's poll fetches it, and the
	 * Transport-Key follows on the poll after that. Answering the request
	 * directly used to deliver the key while the joiner still had its radio
	 * stopped to program the addresses it had just been given.
	 */
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_ASSOC_REQ, NULL);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_ASSOC_RSP);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U,
					     ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ, NULL);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE,
					     NULL);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_NODE_DESC_REQ);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_NODE_DESC_RSP,
					     NULL);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP,
					     NULL);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP,
					     NULL);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ);

	input = zb_host_socket_coord_make_tx(
		0x2202U, 11U, ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP, "native-sim-ed");
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);

	zassert_true(coord.got_timeout_req);
	zassert_true(coord.got_device_announce);
	zassert_true(coord.interview_complete);
	zb_host_socket_coord_observed_model_id(&coord, model_id, sizeof(model_id));
	zassert_str_equal(model_id, "native-sim-ed");
}

ZTEST(host_socket_coordinator, test_permit_join_disabled_rejects_association)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	struct zb_native_sim_socket_medium_msg output;

	zb_host_socket_coord_init(&coord);
	coord.permit_join = false;
	send_filter(&coord);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_ASSOC_REQ, NULL);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	zassert_equal(zb_host_socket_coord_last_assoc_status(&coord), 1);

	/* A refusal is indirect too: the joiner polls for it. */
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	memset(&output, 0, sizeof(output));
	zassert_equal(zb_host_socket_coord_process(&coord, &input, &output), 1);
	zassert_equal(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		      ZB_HOST_SOCKET_FRAME_ASSOC_RSP);
}

ZTEST(host_socket_coordinator, test_native_assoc_request_format_is_accepted)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	struct zb_native_sim_socket_medium_msg output;

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	input = make_native_assoc_req();
	zassert_equal(zb_host_socket_coord_identify_frame(input.psdu, input.psdu_len),
		      ZB_HOST_SOCKET_FRAME_ASSOC_REQ);
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	zassert_equal(zb_host_socket_coord_last_assoc_status(&coord), 0);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	memset(&output, 0, sizeof(output));
	zassert_equal(zb_host_socket_coord_process(&coord, &input, &output), 1);
	zassert_equal(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		      ZB_HOST_SOCKET_FRAME_ASSOC_RSP);
}

ZTEST(host_socket_coordinator, test_transport_key_uses_extended_mac_destination)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	struct zb_native_sim_socket_medium_msg output;

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	input = make_native_assoc_req();
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);

	/* First poll fetches the association response, the next one the key. */
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	memset(&output, 0, sizeof(output));
	zassert_equal(zb_host_socket_coord_process(&coord, &input, &output), 1);
	zassert_equal(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		      ZB_HOST_SOCKET_FRAME_ASSOC_RSP);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	memset(&output, 0, sizeof(output));
	zassert_equal(zb_host_socket_coord_process(&coord, &input, &output), 1);
	zassert_equal(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		      ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);
	zassert_equal(output.psdu_len, 60);
	zassert_equal(output.psdu[0], 0x61);
	zassert_equal(output.psdu[1], 0x8c);
	zassert_equal(output.psdu[3], 0x27);
	zassert_equal(output.psdu[4], 0x5b);
	zassert_equal(output.psdu[5], 0x02);
	zassert_equal(output.psdu[6], 0x00);
	zassert_equal(output.psdu[7], 0x02);
	zassert_equal(output.psdu[8], 0x50);
	zassert_equal(output.psdu[9], 0xe0);
	zassert_equal(output.psdu[10], 0x38);
	zassert_equal(output.psdu[11], 0xc1);
	zassert_equal(output.psdu[12], 0xa4);
	zassert_equal(output.psdu[13], 0x00);
	zassert_equal(output.psdu[14], 0x00);
}

ZTEST(host_socket_coordinator, test_nwk_secured_post_join_frame_is_not_misidentified_as_beacon)
{
	static const uint8_t psdu[] = {
		0x08, 0x02, 0xfd, 0xff, 0x00, 0x27, 0x1e, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x02,
		0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4, 0x01, 0x63, 0x41, 0x9c, 0x3d, 0xf6, 0xb7,
		0x6b, 0x26, 0x3a, 0x5e, 0x9b, 0x23, 0x42, 0xbc, 0x19, 0x88, 0xf6, 0xca, 0x44, 0x34,
		0x2f, 0x66, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	enum zb_host_socket_frame_type type;

	type = zb_host_socket_coord_identify_frame(psdu, sizeof(psdu));
	zassert_true(type != ZB_HOST_SOCKET_FRAME_BEACON);
}

ZTEST(host_socket_coordinator, test_nwk_secured_post_join_device_announce_drives_interview)
{
	static const uint8_t psdu[] = {
		0x08, 0x02, 0xfd, 0xff, 0x00, 0x27, 0x1e, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x02,
		0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4, 0x01, 0x63, 0x41, 0x9c, 0x3d, 0xf6, 0xb7,
		0x6b, 0x26, 0x3a, 0x5e, 0x9b, 0x23, 0x42, 0xbc, 0x19, 0x88, 0xf6, 0xca, 0x44, 0x34,
		0x2f, 0x66, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input = {
		.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX,
		.node_id = 0x2202U,
		.channel = 11U,
		.psdu = psdu,
		.psdu_len = sizeof(psdu),
	};
	struct zb_native_sim_socket_medium_msg poll;

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	zassert_true(coord.got_device_announce);

	poll = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &poll, ZB_HOST_SOCKET_FRAME_NODE_DESC_REQ);
}

ZTEST(host_socket_coordinator, test_medium_model_airtime_formula)
{
	zassert_equal(zb_native_sim_socket_medium_airtime_us(0U), 192U);
	zassert_equal(zb_native_sim_socket_medium_airtime_us(1U), 224U);
	zassert_equal(zb_native_sim_socket_medium_airtime_us(10U), 512U);
	zassert_equal(zb_native_sim_socket_medium_airtime_us(127U), 4256U);
}

ZTEST(host_socket_coordinator, test_medium_model_busy_window_and_collision)
{
	struct zb_native_sim_socket_medium_model model;
	uint64_t busy_until_us = 0U;

	zb_native_sim_socket_medium_model_init(&model);

	zassert_true(!zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1000U));
	zassert_equal(zb_native_sim_socket_medium_model_reserve_window(&model, 11U, 1000U, 1200U, 0,
								       &busy_until_us),
		      ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK);
	zassert_equal(busy_until_us, 1200U);
	zassert_true(zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1000U));
	zassert_true(zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1199U));
	zassert_true(!zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1200U));

	zassert_equal(zb_native_sim_socket_medium_model_reserve_window(&model, 11U, 1000U, 1200U, 0,
								       &busy_until_us),
		      ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK);
	zassert_equal(zb_native_sim_socket_medium_model_reserve_window(&model, 11U, 1100U, 1400U, 0,
								       &busy_until_us),
		      ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_COLLISION);
	zassert_equal(busy_until_us, 1400U);
	zassert_true(zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1399U));
	zassert_true(!zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1400U));
}

ZTEST(host_socket_coordinator, test_medium_model_signal_rssi)
{
	struct zb_native_sim_socket_medium_model model;
	uint64_t busy_until_us = 0U;

	zb_native_sim_socket_medium_model_init(&model);

	zassert_equal(zb_native_sim_socket_medium_model_signal_rssi_dbm(0), -40);
	zassert_equal(zb_native_sim_socket_medium_model_signal_rssi_dbm(8), -32);
	zassert_equal(zb_native_sim_socket_medium_model_signal_rssi_dbm(-80), -96);

	zassert_equal(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1000U, -96),
		      -96);
	zassert_equal(zb_native_sim_socket_medium_model_reserve_window(&model, 11U, 1000U, 1200U, 0,
								       &busy_until_us),
		      ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK);
	zassert_equal(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1100U, -96),
		      -40);
	zassert_equal(zb_native_sim_socket_medium_model_reserve_window(&model, 11U, 1100U, 1400U, 8,
								       &busy_until_us),
		      ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_COLLISION);
	zassert_equal(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1150U, -96),
		      -32);
	zassert_equal(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1400U, -96),
		      -96);
}

ZTEST(host_socket_coordinator, test_status_cca_payload_helpers)
{
	uint8_t payload[8];
	size_t payload_len = 0U;
	bool cca_busy = false;

	zassert_equal(zb_native_sim_socket_medium_status_encode_cca_req(payload, sizeof(payload),
									&payload_len),
		      0);
	zassert_equal(payload_len, 1);
	zassert_true(zb_native_sim_socket_medium_status_is_cca_req(payload, payload_len));

	zassert_equal(zb_native_sim_socket_medium_status_encode_cca_rsp(payload, sizeof(payload),
									true, &payload_len),
		      0);
	zassert_equal(payload_len, 2);
	zassert_equal(
		zb_native_sim_socket_medium_status_decode_cca_rsp(payload, payload_len, &cca_busy),
		0);
	zassert_true(cca_busy);

	zassert_equal(zb_native_sim_socket_medium_status_encode_cca_rsp(payload, sizeof(payload),
									false, &payload_len),
		      0);
	zassert_equal(
		zb_native_sim_socket_medium_status_decode_cca_rsp(payload, payload_len, &cca_busy),
		0);
	zassert_true(!cca_busy);
}

ZTEST(host_socket_coordinator, test_status_tx_result_payload_helpers)
{
	uint8_t payload[8];
	size_t payload_len = 0U;
	bool collision = false;

	zassert_equal(zb_native_sim_socket_medium_status_encode_tx_result_rsp(
			      payload, sizeof(payload), false, &payload_len),
		      0);
	zassert_equal(payload_len, 2);
	zassert_equal(zb_native_sim_socket_medium_status_decode_tx_result_rsp(payload, payload_len,
									      &collision),
		      0);
	zassert_true(!collision);

	zassert_equal(zb_native_sim_socket_medium_status_encode_tx_result_rsp(
			      payload, sizeof(payload), true, &payload_len),
		      0);
	zassert_equal(payload_len, 2);
	zassert_equal(zb_native_sim_socket_medium_status_decode_tx_result_rsp(payload, payload_len,
									      &collision),
		      0);
	zassert_true(collision);
}

ZTEST(host_socket_coordinator, test_assoc_response_without_joiner_uses_default_ieee)
{
	struct zb_native_sim_socket_medium_msg msg;

	msg = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_ASSOC_RSP, NULL);
	zassert_equal(zb_host_socket_coord_identify_frame(msg.psdu, msg.psdu_len),
		      ZB_HOST_SOCKET_FRAME_ASSOC_RSP);
}

static struct zb_native_sim_socket_medium_msg make_data_req_from(uint16_t src_short)
{
	/* MAC command, short destination and source, PAN ID compression. */
	static uint8_t psdu[] = {
		0x63, 0x88, 0x10, 0x27, 0x5b, 0x00, 0x00, 0x00, 0x00, 0x04,
	};
	struct zb_native_sim_socket_medium_msg msg = {
		.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX,
		.node_id = 0x2202U,
		.channel = 11U,
		.psdu = psdu,
		.psdu_len = sizeof(psdu),
	};

	psdu[7] = (uint8_t)src_short;
	psdu[8] = (uint8_t)(src_short >> 8);
	return msg;
}

ZTEST(host_socket_coordinator, test_transport_key_retry_answers_only_the_joiner)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	struct zb_native_sim_socket_medium_msg output;

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	input = make_native_assoc_req();
	zassert_equal(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_ASSOC_RSP);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);

	/* The queue is empty now; another node's poll gets nothing. */
	input = make_data_req_from(0x1234U);
	memset(&output, 0, sizeof(output));
	zassert_equal(zb_host_socket_coord_process(&coord, &input, &output), 0);

	/* The joiner, polling from the short address it was given, gets its key again. */
	input = make_data_req_from(coord.child_short);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);
}

ZTEST_SUITE(host_socket_coordinator, NULL, NULL, NULL, NULL, NULL);
