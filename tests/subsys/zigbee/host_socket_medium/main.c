/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/zigbee/native_sim_socket_medium.h>

static int failures;

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		printf("FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		printf("FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #expr); \
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

#define EXPECT_MEM_EQ(actual, expected, len) do { \
	if (memcmp((actual), (expected), (len)) != 0) { \
		printf("FAIL %s:%d: memory mismatch: %s\n", __FILE__, __LINE__, #actual); \
		failures++; \
	} \
} while (0)

static void test_round_trip_tx_message(void)
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
	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(encoded_len > sizeof(psdu));

	memset(&parsed, 0, sizeof(parsed));
	rc = zb_native_sim_socket_medium_decode(&parsed, buffer, encoded_len);
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(parsed.type, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX);
	EXPECT_EQ(parsed.node_id, 0x2202U);
	EXPECT_EQ(parsed.channel, 11U);
	EXPECT_EQ(parsed.tx_power_dbm, 3);
	EXPECT_EQ(parsed.psdu_len, sizeof(psdu));
	EXPECT_MEM_EQ(parsed.psdu, psdu, sizeof(psdu));
}

static void test_filter_update_controls_medium_state(void)
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
	EXPECT_EQ(rc, 0);
	EXPECT_EQ(peer.node_id, 0x2202U);
	EXPECT_EQ(peer.channel, 11U);
	EXPECT_TRUE(peer.rx_on);
	EXPECT_EQ(peer.pan_id, 0x5b27U);
	EXPECT_EQ(peer.short_addr, 0x2700U);
	EXPECT_MEM_EQ(peer.ieee_addr, ieee_addr, sizeof(ieee_addr));
}

static void test_filter_matching_uses_pan_and_short_address(void)
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

	EXPECT_TRUE(zb_native_sim_socket_medium_peer_accepts_psdu(&peer, accepted_psdu,
								 sizeof(accepted_psdu)));
	EXPECT_FALSE(zb_native_sim_socket_medium_peer_accepts_psdu(&peer, rejected_pan_psdu,
								  sizeof(rejected_pan_psdu)));
	EXPECT_FALSE(zb_native_sim_socket_medium_peer_accepts_psdu(&peer,
								  rejected_short_psdu,
								  sizeof(rejected_short_psdu)));
}

int main(void)
{
	test_round_trip_tx_message();
	test_filter_update_controls_medium_state();
	test_filter_matching_uses_pan_and_short_address();

	if (failures != 0) {
		printf("host_socket_medium: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_socket_medium: PASS\n");
	return 0;
}
