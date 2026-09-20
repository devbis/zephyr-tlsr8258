/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Behavioural unit test for the TLSR8258 radio auto-ACK decision logic
 * (drivers/ieee802154/ieee802154_tlsr8258_ack_filter.h), which the driver's
 * tlsr8258_ack_requested()/tlsr8258_filter_match_for_ack() wrap verbatim.
 *
 * Motivation: on real hardware (2026-07-06) the router associated with the z2m
 * coordinator (coord granted short 0x225A, status SUCCESS) but never MAC-ACKed
 * the association RESPONSE, which is addressed to the router by its IEEE/EUI-64.
 * The coordinator retransmitted 4x and discarded the join. zb_acktx_trace showed
 * only 1 ACK sent out of 7482 ACK-requested frames. This test pins down the
 * ACK-decision contract using the exact bytes captured on air, so the fix can be
 * validated on the host and regressions caught.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ieee802154_tlsr8258_ack_filter.h"

static int failures;

#define EXPECT_EQ_BOOL(expr, want)                                                       \
	do {                                                                             \
		bool _got = (expr);                                                      \
		bool _want = (want);                                                     \
		if (_got != _want) {                                                     \
			fprintf(stderr, "FAIL %s:%d: %s -> %s, expected %s\n", __FILE__,  \
				__LINE__, #expr, _got ? "true" : "false",               \
				_want ? "true" : "false");                              \
			failures++;                                                     \
		}                                                                       \
	} while (0)

/*
 * Real MAC PSDU of the coordinator's Association Response, captured on air
 * (pcap frame 480):
 *   FCF   = 0xcc63  (command, ACK-request set, dst addr mode = extended/IEEE)
 *   seq   = 0x7f
 *   dPAN  = 0x1a62  (the z2m network)
 *   dst64 = 0c 00 02 50 e0 38 c1 a4  (router IEEE a4:c1:38:e0:50:02:00:0c, LE)
 *   src64 = 0c 80 1e fe ff 16 a7 20  (coordinator)
 *   cmd   = 0x02  (assoc response), short 0x225a, status 0x00 (SUCCESS)
 */
static const uint8_t assoc_resp[] = {
	0x63, 0xcc, 0x7f, 0x62, 0x1a, 0x0c, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1,
	0xa4, 0x0c, 0x80, 0x1e, 0xfe, 0xff, 0x16, 0xa7, 0x20, 0x02, 0x5a, 0x22, 0x00,
};

/* Our EUI-64 as stored in the radio filter (little-endian on air). */
static const uint8_t our_ieee[8] = {0x0c, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4};
static const uint8_t other_ieee[8] = {0x99, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4};

/* PAN-id filter values (little-endian, as stored in the radio). */
static const uint8_t pan_wildcard[2] = {0xff, 0xff}; /* pre-association */
static const uint8_t pan_z2m[2] = {0x62, 0x1a};      /* 0x1a62, the joining PAN */
static const uint8_t pan_other[2] = {0xce, 0x6a};    /* 0x6ace, a neighbouring net */

static const uint8_t short_wildcard[2] = {0xff, 0xff};

/*
 * A short-addressed data frame to short 0x225a on PAN 0x1a62:
 *   FCF = 0x8861 (data, ACK-request set, dst+src short), dPAN 0x1a62, dst 0x225a.
 */
static const uint8_t data_to_225a[] = {
	0x61, 0x88, 0x10, 0x62, 0x1a, 0x5a, 0x22, 0x00, 0x00, 0x00,
};
/* Same but destined to a different short (0x1234). */
static const uint8_t data_to_1234[] = {
	0x61, 0x88, 0x11, 0x62, 0x1a, 0x34, 0x12, 0x00, 0x00, 0x00,
};
/* Same but broadcast short (0xffff). */
static const uint8_t data_to_bcast[] = {
	0x61, 0x88, 0x12, 0x62, 0x1a, 0xff, 0xff, 0x00, 0x00, 0x00,
};
static const uint8_t short_225a[2] = {0x5a, 0x22};

static void test_ack_requested(void)
{
	/* Assoc response requests an ACK (bit set, frame type = command). */
	EXPECT_EQ_BOOL(tlsr8258_ackf_ack_requested(assoc_resp, sizeof(assoc_resp)), true);

	/* A bare ACK frame (type 0x02) never itself requests an ACK. */
	const uint8_t ack_frame[] = {0x02, 0x00, 0x2a};
	EXPECT_EQ_BOOL(tlsr8258_ackf_ack_requested(ack_frame, sizeof(ack_frame)), false);

	/* ACK-request bit clear -> no ACK. */
	const uint8_t no_ackreq[] = {0x41, 0x88, 0x00, 0x62, 0x1a, 0x5a, 0x22, 0x00};
	EXPECT_EQ_BOOL(tlsr8258_ackf_ack_requested(no_ackreq, sizeof(no_ackreq)), false);

	/* Runt frame -> no ACK. */
	EXPECT_EQ_BOOL(tlsr8258_ackf_ack_requested(assoc_resp, 2u), false);
}

static void test_assoc_response_ieee_match(void)
{
	/*
	 * THE CORE CONTRACT. The association response is addressed to our IEEE.
	 * It MUST be ACKed whenever the filter holds our IEEE and the filter PAN
	 * is either the wildcard (pre-association) or the joining PAN. If either
	 * of these regresses, the router silently drops the join on hardware.
	 */
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(assoc_resp, pan_wildcard,
							short_wildcard, our_ieee),
		       true);
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(assoc_resp, pan_z2m,
							short_wildcard, our_ieee),
		       true);

	/* Not addressed to us (different IEEE) -> must NOT ACK. */
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(assoc_resp, pan_z2m,
							short_wildcard, other_ieee),
		       false);

	/*
	 * Join-specific guardrail: a stale non-wildcard PAN left by scan must NOT
	 * block the successful Association Response addressed to our IEEE while our
	 * short address is still unassigned (0xffff). Otherwise the coordinator
	 * retries the response and the router never learns its short address.
	 */
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(assoc_resp, pan_other,
							short_wildcard, our_ieee),
		       true);
}

static void test_short_dest_match(void)
{
	/* Addressed to our short address on our PAN -> ACK. */
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(data_to_225a, pan_z2m,
							short_225a, our_ieee),
		       true);

	/* Addressed to a different short address -> no ACK. */
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(data_to_1234, pan_z2m,
							short_225a, our_ieee),
		       false);

	/* Broadcast short destination -> always ACK-eligible. */
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(data_to_bcast, pan_z2m,
							short_225a, our_ieee),
		       true);

	/* Unassigned short filter (0xffff) + unicast dest -> no ACK. */
	EXPECT_EQ_BOOL(tlsr8258_ackf_dst_matches_filter(data_to_225a, pan_z2m,
							short_wildcard, our_ieee),
		       false);
}

int main(void)
{
	test_ack_requested();
	test_assoc_response_ieee_match();
	test_short_dest_match();

	if (failures != 0) {
		fprintf(stderr, "zigbee_ack_filter_match: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_ack_filter_match: PASS\n");
	return 0;
}
