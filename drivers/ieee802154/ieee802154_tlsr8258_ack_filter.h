/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Pure, Zephyr-independent 802.15.4 auto-ACK decision logic for the TLSR8258
 * radio, factored out of ieee802154_tlsr8258.c so it can be unit-tested on the
 * host without the full driver / RF-register surface.
 *
 * The driver's tlsr8258_ack_requested()/tlsr8258_filter_match_for_ack() are
 * thin wrappers over these inlines, and tests/unit/zigbee_ack_filter_match
 * exercises them with real on-air byte vectors captured from hardware.
 *
 * Depends only on <stdint.h>/<stdbool.h>/<string.h> so the same code compiles
 * both in the Zephyr driver and in a plain host unit test.
 */
#ifndef ZEPHYR_DRIVERS_IEEE802154_TLSR8258_ACK_FILTER_H_
#define ZEPHYR_DRIVERS_IEEE802154_TLSR8258_ACK_FILTER_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * 802.15.4 frame-format offsets for the PAN-ID-compressed frames this radio
 * auto-ACKs: FCF[0..1], seq[2], dst PAN[3..4], dst addr[5..]. These are fixed
 * by the standard for the frames we ACK, so they do not track any PIB state.
 */
#define TLSR8258_ACKF_FRAME_TYPE_OFFSET     0u
#define TLSR8258_ACKF_ACK_REQUEST_BIT       (1u << 5)
#define TLSR8258_ACKF_DEST_ADDR_TYPE_OFFSET 1u
#define TLSR8258_ACKF_DEST_ADDR_TYPE_MASK   0x0cu
#define TLSR8258_ACKF_DEST_ADDR_TYPE_SHORT  0x08u
#define TLSR8258_ACKF_DEST_ADDR_TYPE_IEEE   0x0cu
#define TLSR8258_ACKF_PAN_ID_OFFSET         3u
#define TLSR8258_ACKF_DEST_ADDR_OFFSET      5u
#define TLSR8258_ACKF_IEEE_ADDR_SIZE        8u
#define TLSR8258_ACKF_MIN_FRAME_LENGTH      3u

static inline uint16_t tlsr8258_ackf_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/*
 * True iff an inbound frame requests a MAC ACK: the ACK-request bit is set and
 * the frame is not itself an ACK (frame type 0x02).
 */
static inline bool tlsr8258_ackf_ack_requested(const uint8_t *payload, uint8_t length)
{
	if (length < TLSR8258_ACKF_MIN_FRAME_LENGTH) {
		return false;
	}

	if ((payload[0] & TLSR8258_ACKF_ACK_REQUEST_BIT) == 0u) {
		return false;
	}

	return (payload[TLSR8258_ACKF_FRAME_TYPE_OFFSET] & 0x07u) != 0x02u;
}

/*
 * True iff an inbound frame's destination matches our address filter and we
 * should therefore auto-ACK it.
 *
 *   filter_pan_id/filter_short_addr : 2-byte little-endian, as stored in the
 *                                     radio filter registers.
 *   filter_ieee_addr                : 8-byte little-endian (our EUI-64).
 *
 * A wildcard filter PAN (0xffff, i.e. pre-association / pre-rejoin) matches any
 * PAN; a broadcast destination short address (0xffff) always matches. An
 * extended (EUI-64) destination matches only our exact IEEE address.
 */
static inline bool tlsr8258_ackf_dst_matches_filter(const uint8_t *payload,
						    const uint8_t *filter_pan_id,
						    const uint8_t *filter_short_addr,
						    const uint8_t *filter_ieee_addr)
{
	uint16_t filter_pan = tlsr8258_ackf_le16(filter_pan_id);
	uint16_t payload_pan = tlsr8258_ackf_le16(&payload[TLSR8258_ACKF_PAN_ID_OFFSET]);
	uint16_t filter_short = tlsr8258_ackf_le16(filter_short_addr);
	uint8_t dst_mode = payload[TLSR8258_ACKF_DEST_ADDR_TYPE_OFFSET] &
			   TLSR8258_ACKF_DEST_ADDR_TYPE_MASK;

	/*
	 * During association the coordinator's successful Association Response is
	 * addressed to our IEEE while our short filter is still 0xffff. If scan /
	 * pre-join code left a non-wildcard PAN in the filter, rejecting that frame
	 * on PAN mismatch drops the join even though the IEEE destination matches us.
	 * Treat "IEEE dst to us while short is unassigned" as pre-association and
	 * let the IEEE match decide.
	 */
	if ((dst_mode == TLSR8258_ACKF_DEST_ADDR_TYPE_IEEE) && (filter_short == 0xffffu) &&
	    (memcmp(&payload[TLSR8258_ACKF_DEST_ADDR_OFFSET], filter_ieee_addr,
		    TLSR8258_ACKF_IEEE_ADDR_SIZE) == 0)) {
		return true;
	}

	if ((filter_pan != 0xffffu) && (payload_pan != filter_pan) && (payload_pan != 0xffffu)) {
		return false;
	}

	switch (dst_mode) {
	case TLSR8258_ACKF_DEST_ADDR_TYPE_SHORT: {
		uint16_t payload_short = tlsr8258_ackf_le16(&payload[TLSR8258_ACKF_DEST_ADDR_OFFSET]);

		if (payload_short == 0xffffu) {
			return true;
		}

		return (filter_short != 0xffffu) && (payload_short == filter_short);
	}
	case TLSR8258_ACKF_DEST_ADDR_TYPE_IEEE:
		return memcmp(&payload[TLSR8258_ACKF_DEST_ADDR_OFFSET], filter_ieee_addr,
			      TLSR8258_ACKF_IEEE_ADDR_SIZE) == 0;
	default:
		return false;
	}
}

#endif /* ZEPHYR_DRIVERS_IEEE802154_TLSR8258_ACK_FILTER_H_ */
