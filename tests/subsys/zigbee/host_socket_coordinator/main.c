/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/zigbee/native_sim_socket_medium.h>
#include <zephyr/zigbee/native_sim_socket_medium_model.h>

#include "coord_logic.h"

static int failures;

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		printf("FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_EQ(actual, expected) do { \
	long long _actual = (long long)(actual); \
	long long _expected = (long long)(expected); \
	if (_actual != _expected) { \
		printf("FAIL %s:%d: %s=%lld expected %lld\n", __FILE__, __LINE__, \
		       #actual, _actual, _expected); \
		failures++; \
	} \
} while (0)

#define EXPECT_STR_EQ(actual, expected) do { \
	if (strcmp((actual), (expected)) != 0) { \
		printf("FAIL %s:%d: %s=\"%s\" expected \"%s\"\n", __FILE__, __LINE__, \
		       #actual, (actual), (expected)); \
		failures++; \
	} \
} while (0)

static void send_filter(struct zb_host_socket_coord *coord)
{
	static const uint8_t ieee_addr[] = { 0x02, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4 };
	struct zb_native_sim_socket_medium_msg filter = {
		.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER,
		.node_id = 0x2202U,
		.pan_id = 0x5b27U,
		.short_addr = 0xffffU,
		.channel = 11U,
		.rx_on = true,
	};

	memcpy(filter.ieee_addr, ieee_addr, sizeof(filter.ieee_addr));
	EXPECT_EQ(zb_host_socket_coord_process(coord, &filter, NULL), 0);
}

static struct zb_native_sim_socket_medium_msg make_native_assoc_req(void)
{
	static const uint8_t psdu[] = {
		0x63, 0xC8, 0x01,
		0x27, 0x5B,
		0x00, 0x00,
		0x02, 0x00, 0x02, 0x50, 0xE0, 0x38, 0xC1, 0xA4,
		0x01,
		0x80,
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
	EXPECT_EQ(zb_host_socket_coord_process(coord, input, &output), 1);
	EXPECT_EQ(output.type, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX);
	actual_type = zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len);
	EXPECT_EQ(actual_type, expected_type);
}

static void test_join_and_interview_flow(void)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	char model_id[32];

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_ASSOC_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_ASSOC_RSP);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U,
					     ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ, NULL);
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE, NULL);
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, NULL), 0);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP, NULL);
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP, NULL);
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, NULL), 0);
	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	expect_single_output(&coord, &input, ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U,
					     ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP,
					     "native-sim-ed");
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, NULL), 0);

	EXPECT_TRUE(coord.got_timeout_req);
	EXPECT_TRUE(coord.got_device_announce);
	EXPECT_TRUE(coord.interview_complete);
	zb_host_socket_coord_observed_model_id(&coord, model_id, sizeof(model_id));
	EXPECT_STR_EQ(model_id, "native-sim-ed");
}

static void test_permit_join_disabled_rejects_association(void)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	struct zb_native_sim_socket_medium_msg output;

	zb_host_socket_coord_init(&coord);
	coord.permit_join = false;
	send_filter(&coord);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_ASSOC_REQ, NULL);
	memset(&output, 0, sizeof(output));
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, &output), 1);
	EXPECT_EQ(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		  ZB_HOST_SOCKET_FRAME_ASSOC_RSP);
	EXPECT_EQ(zb_host_socket_coord_last_assoc_status(&coord), 1);
}

static void test_native_assoc_request_format_is_accepted(void)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	struct zb_native_sim_socket_medium_msg output;

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	input = make_native_assoc_req();
	EXPECT_EQ(zb_host_socket_coord_identify_frame(input.psdu, input.psdu_len),
		  ZB_HOST_SOCKET_FRAME_ASSOC_REQ);
	memset(&output, 0, sizeof(output));
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, &output), 1);
	EXPECT_EQ(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		  ZB_HOST_SOCKET_FRAME_ASSOC_RSP);
	EXPECT_EQ(zb_host_socket_coord_last_assoc_status(&coord), 0);
}

static void test_transport_key_uses_extended_mac_destination(void)
{
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_msg input;
	struct zb_native_sim_socket_medium_msg output;

	zb_host_socket_coord_init(&coord);
	send_filter(&coord);

	input = make_native_assoc_req();
	memset(&output, 0, sizeof(output));
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, &output), 1);
	EXPECT_EQ(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		  ZB_HOST_SOCKET_FRAME_ASSOC_RSP);

	input = zb_host_socket_coord_make_tx(0x2202U, 11U, ZB_HOST_SOCKET_FRAME_DATA_REQ, NULL);
	memset(&output, 0, sizeof(output));
	EXPECT_EQ(zb_host_socket_coord_process(&coord, &input, &output), 1);
	EXPECT_EQ(zb_host_socket_coord_identify_frame(output.psdu, output.psdu_len),
		  ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);
	EXPECT_EQ(output.psdu_len, 60);
	EXPECT_EQ(output.psdu[0], 0x61);
	EXPECT_EQ(output.psdu[1], 0x8c);
	EXPECT_EQ(output.psdu[3], 0x27);
	EXPECT_EQ(output.psdu[4], 0x5b);
	EXPECT_EQ(output.psdu[5], 0x02);
	EXPECT_EQ(output.psdu[6], 0x00);
	EXPECT_EQ(output.psdu[7], 0x02);
	EXPECT_EQ(output.psdu[8], 0x50);
	EXPECT_EQ(output.psdu[9], 0xe0);
	EXPECT_EQ(output.psdu[10], 0x38);
	EXPECT_EQ(output.psdu[11], 0xc1);
	EXPECT_EQ(output.psdu[12], 0xa4);
	EXPECT_EQ(output.psdu[13], 0x00);
	EXPECT_EQ(output.psdu[14], 0x00);
}

static void test_medium_model_airtime_formula(void)
{
	EXPECT_EQ(zb_native_sim_socket_medium_airtime_us(0U), 192U);
	EXPECT_EQ(zb_native_sim_socket_medium_airtime_us(1U), 224U);
	EXPECT_EQ(zb_native_sim_socket_medium_airtime_us(10U), 512U);
	EXPECT_EQ(zb_native_sim_socket_medium_airtime_us(127U), 4256U);
}

static void test_medium_model_busy_window_and_collision(void)
{
	struct zb_native_sim_socket_medium_model model;
	uint64_t busy_until_us = 0U;

	zb_native_sim_socket_medium_model_init(&model);

	EXPECT_TRUE(!zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1000U));
	EXPECT_EQ(zb_native_sim_socket_medium_model_reserve_window(
			  &model, 11U, 1000U, 1200U, 0, &busy_until_us),
		  ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK);
	EXPECT_EQ(busy_until_us, 1200U);
	EXPECT_TRUE(zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1000U));
	EXPECT_TRUE(zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1199U));
	EXPECT_TRUE(!zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1200U));

	EXPECT_EQ(zb_native_sim_socket_medium_model_reserve_window(
			  &model, 11U, 1000U, 1200U, 0, &busy_until_us),
		  ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK);
	EXPECT_EQ(zb_native_sim_socket_medium_model_reserve_window(
			  &model, 11U, 1100U, 1400U, 0, &busy_until_us),
		  ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_COLLISION);
	EXPECT_EQ(busy_until_us, 1400U);
	EXPECT_TRUE(zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1399U));
	EXPECT_TRUE(!zb_native_sim_socket_medium_model_channel_busy(&model, 11U, 1400U));
}

static void test_medium_model_signal_rssi(void)
{
	struct zb_native_sim_socket_medium_model model;
	uint64_t busy_until_us = 0U;

	zb_native_sim_socket_medium_model_init(&model);

	EXPECT_EQ(zb_native_sim_socket_medium_model_signal_rssi_dbm(0), -40);
	EXPECT_EQ(zb_native_sim_socket_medium_model_signal_rssi_dbm(8), -32);
	EXPECT_EQ(zb_native_sim_socket_medium_model_signal_rssi_dbm(-80), -96);

	EXPECT_EQ(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1000U, -96), -96);
	EXPECT_EQ(zb_native_sim_socket_medium_model_reserve_window(
			  &model, 11U, 1000U, 1200U, 0, &busy_until_us),
		  ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK);
	EXPECT_EQ(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1100U, -96), -40);
	EXPECT_EQ(zb_native_sim_socket_medium_model_reserve_window(
			  &model, 11U, 1100U, 1400U, 8, &busy_until_us),
		  ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_COLLISION);
	EXPECT_EQ(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1150U, -96), -32);
	EXPECT_EQ(zb_native_sim_socket_medium_model_channel_rssi_dbm(&model, 11U, 1400U, -96), -96);
}

static void test_status_cca_payload_helpers(void)
{
	uint8_t payload[8];
	size_t payload_len = 0U;
	bool cca_busy = false;

	EXPECT_EQ(zb_native_sim_socket_medium_status_encode_cca_req(payload, sizeof(payload),
								    &payload_len), 0);
	EXPECT_EQ(payload_len, 1);
	EXPECT_TRUE(zb_native_sim_socket_medium_status_is_cca_req(payload, payload_len));

	EXPECT_EQ(zb_native_sim_socket_medium_status_encode_cca_rsp(payload, sizeof(payload), true,
								    &payload_len), 0);
	EXPECT_EQ(payload_len, 2);
	EXPECT_EQ(zb_native_sim_socket_medium_status_decode_cca_rsp(payload, payload_len,
								    &cca_busy), 0);
	EXPECT_TRUE(cca_busy);

	EXPECT_EQ(zb_native_sim_socket_medium_status_encode_cca_rsp(payload, sizeof(payload), false,
								    &payload_len), 0);
	EXPECT_EQ(zb_native_sim_socket_medium_status_decode_cca_rsp(payload, payload_len,
								    &cca_busy), 0);
	EXPECT_TRUE(!cca_busy);
}

static void test_status_tx_result_payload_helpers(void)
{
	uint8_t payload[8];
	size_t payload_len = 0U;
	bool collision = false;

	EXPECT_EQ(zb_native_sim_socket_medium_status_encode_tx_result_rsp(payload,
									  sizeof(payload),
									  false,
									  &payload_len), 0);
	EXPECT_EQ(payload_len, 2);
	EXPECT_EQ(zb_native_sim_socket_medium_status_decode_tx_result_rsp(payload, payload_len,
									    &collision), 0);
	EXPECT_TRUE(!collision);

	EXPECT_EQ(zb_native_sim_socket_medium_status_encode_tx_result_rsp(payload,
									  sizeof(payload),
									  true,
									  &payload_len), 0);
	EXPECT_EQ(payload_len, 2);
	EXPECT_EQ(zb_native_sim_socket_medium_status_decode_tx_result_rsp(payload, payload_len,
									    &collision), 0);
	EXPECT_TRUE(collision);
}

int main(void)
{
	test_join_and_interview_flow();
	test_permit_join_disabled_rejects_association();
	test_native_assoc_request_format_is_accepted();
	test_transport_key_uses_extended_mac_destination();
	test_medium_model_airtime_formula();
	test_medium_model_busy_window_and_collision();
	test_medium_model_signal_rssi();
	test_status_cca_payload_helpers();
	test_status_tx_result_payload_helpers();

	if (failures != 0) {
		printf("host_socket_coordinator: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_socket_coordinator: PASS\n");
	return 0;
}
