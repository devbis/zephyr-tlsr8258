/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host reproduction of the RX-ISR MAC-ACK decision (rx_capture_common's
 * self_originated / ack_requested / should_ack chain), exercised via the
 * shared tlsr8258_core_rx_ack_decision() in fake_phy_core.h.
 *
 * Motivation: on hardware a counter showed send_ack_if_needed() was called 0
 * times over ~1019 received frames (ack_requested apparently false for every
 * frame), yet the device joined and the sniffer showed ACKs. This test feeds
 * the SAME decision the ISR runs a REAL captured coordinator->us Node-Desc-Req
 * (ack-requested unicast) plus synthetic frames, to prove the logic decides
 * "ACK it" for such a frame — isolating the HW anomaly to a runtime data/offset
 * issue rather than the decision logic.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ieee802154_tlsr8258_fake_phy_core.h"

static int failures;

#define CHECK(cond, msg)                                                          \
	do {                                                                      \
		if (!(cond)) {                                                    \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
			failures++;                                               \
		}                                                                 \
	} while (0)

/* Our own IEEE (a4:c1:38:e0:50:02:00:0c), little-endian on air. */
static const uint8_t our_ieee[8] = {0x0c, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4};
/* Our assigned short 0x38d9 and the PAN 0x1a62 from the capture, little-endian. */
static const uint8_t our_short[2] = {0xd9, 0x38};
static const uint8_t wild_short[2] = {0xff, 0xff};
static const uint8_t pan[2] = {0x62, 0x1a};

/*
 * REAL coordinator(0x0000)->us(0x38d9) ack-requested Node-Desc-Req PSDU,
 * captured on air (/tmp/zb-s3.pcap @13.839s), FCS trimmed. FCF=0x8861:
 * DATA type, ack-request bit set, short dst 0x38d9, short src 0x0000.
 */
static const uint8_t real_coord_to_us[] = {
	0x61, 0x88, 0x3d, 0x62, 0x1a, 0xd9, 0x38, 0x00, 0x00, 0x08,
	0x00, 0xd9, 0x38, 0x00, 0x00, 0x1e, 0x9a, 0x21, 0xe7, 0x30,
	0x04, 0x50, 0x00, 0x00, 0x0c, 0x80, 0x1e, 0xfe, 0xff, 0x16,
};

static void test_real_coord_req_gets_acked(void)
{
	struct tlsr8258_core_filter_ctx f = {.pan_id = pan, .short_addr = our_short,
					     .ieee_addr = our_ieee};
	struct tlsr8258_core_rx_ack_decision d;

	tlsr8258_core_rx_ack_decision(real_coord_to_us, sizeof(real_coord_to_us), &f, &d);

	CHECK(!d.self_originated, "real coord req must not be self-originated (DATA type)");
	CHECK(d.ack_requested, "real coord req has FCF ack-request bit set");
	CHECK(d.filter_match, "real coord req dst 0x38d9 == our short -> filter match");
	CHECK(d.should_ack, "we MUST MAC-ACK the coordinator's Node-Desc-Req");
}

static void test_same_req_before_join_not_ours(void)
{
	/* Pre-join our short is 0xffff; the frame's dst 0x38d9 is not us nor bcast. */
	struct tlsr8258_core_filter_ctx f = {.pan_id = pan, .short_addr = wild_short,
					     .ieee_addr = our_ieee};
	struct tlsr8258_core_rx_ack_decision d;

	tlsr8258_core_rx_ack_decision(real_coord_to_us, sizeof(real_coord_to_us), &f, &d);

	CHECK(d.ack_requested, "ack-request bit still set");
	CHECK(!d.filter_match, "dst 0x38d9 != our (unassigned) short -> no match");
	CHECK(!d.should_ack, "must not ACK a unicast to a short we do not own");
}

static void test_unicast_to_other_short_not_acked(void)
{
	uint8_t frame[sizeof(real_coord_to_us)];
	struct tlsr8258_core_filter_ctx f = {.pan_id = pan, .short_addr = our_short,
					     .ieee_addr = our_ieee};
	struct tlsr8258_core_rx_ack_decision d;

	memcpy(frame, real_coord_to_us, sizeof(frame));
	frame[5] = 0x34; /* dst short -> 0x1234 (someone else) */
	frame[6] = 0x12;

	tlsr8258_core_rx_ack_decision(frame, sizeof(frame), &f, &d);

	CHECK(d.ack_requested, "ack-request bit set");
	CHECK(!d.filter_match, "dst 0x1234 is another node -> no match");
	CHECK(!d.should_ack, "must not ACK another node's unicast");
}

static void test_broadcast_matches_filter(void)
{
	uint8_t frame[sizeof(real_coord_to_us)];
	struct tlsr8258_core_filter_ctx f = {.pan_id = pan, .short_addr = our_short,
					     .ieee_addr = our_ieee};
	struct tlsr8258_core_rx_ack_decision d;

	memcpy(frame, real_coord_to_us, sizeof(frame));
	frame[5] = 0xff; /* dst short -> 0xffff broadcast */
	frame[6] = 0xff;

	tlsr8258_core_rx_ack_decision(frame, sizeof(frame), &f, &d);

	CHECK(d.filter_match, "broadcast dst always matches the filter");
}

static void test_own_data_request_is_self_originated(void)
{
	/*
	 * Our own Data-Request poll echo: MAC command (type 3), ack-request set,
	 * dst=coord short 0x0000, src=our short 0x38d9, PAN-compressed, cmd 0x04.
	 * FCF=0x8863.
	 */
	const uint8_t own_poll[] = {
		0x63, 0x88, 0x11, 0x62, 0x1a, 0x00, 0x00, 0xd9, 0x38, 0x04,
	};
	struct tlsr8258_core_filter_ctx f = {.pan_id = pan, .short_addr = our_short,
					     .ieee_addr = our_ieee};
	struct tlsr8258_core_rx_ack_decision d;

	tlsr8258_core_rx_ack_decision(own_poll, sizeof(own_poll), &f, &d);

	CHECK(d.self_originated, "our own data-request echo is self-originated");
	CHECK(!d.ack_requested, "self-originated frames are never treated as ack-requested");
	CHECK(!d.should_ack, "must not ACK our own echoed poll");
}

int main(void)
{
	test_real_coord_req_gets_acked();
	test_same_req_before_join_not_ours();
	test_unicast_to_other_short_not_acked();
	test_broadcast_matches_filter();
	test_own_data_request_is_self_originated();

	if (failures != 0) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("tlsr8258_rx_ack_decision: all checks passed\n");
	return 0;
}
