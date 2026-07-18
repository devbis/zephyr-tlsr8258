/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_TLSR8258_FAKE_PHY_CORE_H_
#define ZEPHYR_DRIVERS_IEEE802154_TLSR8258_FAKE_PHY_CORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ieee802154_tlsr8258_ack_filter.h"
#include "ieee802154_tlsr8258_tx_irq.h"

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

struct tlsr8258_radio_backend {
	uint16_t (*get_irq_status)(void *ctx);
	void (*clear_irq_status)(void *ctx, uint16_t bits);
	void (*set_mode_rx)(void *ctx);
	void (*set_mode_rx_fast)(void *ctx);
	void (*set_mode_tx)(void *ctx);
	void (*set_rx_buffer)(void *ctx, uint8_t *buf, uint16_t len);
	void (*tx_start_ack)(void *ctx, const uint8_t *ack, uint8_t len);
	uint32_t (*now_cycles)(void *ctx);
};

struct tlsr8258_core_filter_ctx {
	const uint8_t *pan_id;
	const uint8_t *short_addr;
	const uint8_t *ieee_addr;
};

struct tlsr8258_core_tx_done_result {
	bool tx_success;
	bool tx_had_rx;
	bool enter_rx_fast;
	bool rearm_rx_buffer;
	bool clear_ack_tx_pending;
	bool count_ack_tx_completion;
	bool defer_stack_tx_to_rx_path;
	bool complete_stack_tx;
};

struct tlsr8258_core_rx_result {
	bool ack_requested;
	bool ack_eligible;
	bool is_ack;
	bool ack_pending;
	bool is_pending_response;
	bool assoc_resp_to_ieee;
	uint8_t ack_seq;
};

struct tlsr8258_core_rx_dma_result {
	uint8_t *rx_proc;
	uint8_t *next_rx_active;
	bool rearm_rx_buffer;
};

struct tlsr8258_core_rx_only_tx_result {
	bool saw_rx_while_tx_pending;
	bool complete_stack_tx;
};

enum tlsr8258_core_post_tx_rx_classification {
	TLSR8258_CORE_POST_TX_RX_NO_EVENT = 0,
	TLSR8258_CORE_POST_TX_RX_NO_RAW_TIMEOUT,
	TLSR8258_CORE_POST_TX_RX_RAW_DROPPED,
	TLSR8258_CORE_POST_TX_RX_RX_HANDOFF,
};

struct tlsr8258_core_post_tx_rx_window_result {
	bool window_armed_after;
	bool timed_out_without_raw_rx;
	bool saw_raw_rx;
	bool saw_valid_rx_handoff;
	enum tlsr8258_core_post_tx_rx_classification classification;
};

static inline uint16_t tlsr8258_core_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint8_t *tlsr8258_core_next_rx_buffer(uint8_t *current, uint8_t *buf_a,
						     uint8_t *buf_b)
{
	return (current == buf_a) ? buf_b : buf_a;
}

static inline uint8_t tlsr8258_core_mac_hdr_size(uint16_t fcf, uint8_t psdu_len)
{
	uint8_t hdr_len = 3u;
	uint8_t dst_mode = (uint8_t)((fcf >> 10) & 0x03u);
	uint8_t src_mode = (uint8_t)((fcf >> 14) & 0x03u);
	bool pan_compression = (fcf & BIT(6)) != 0u;

	if (dst_mode != 0u) {
		hdr_len += 2u;
		hdr_len += (dst_mode == 0x03u) ? 8u : 2u;
	}

	if (src_mode != 0u) {
		if (!pan_compression || dst_mode == 0u) {
			hdr_len += 2u;
		}
		hdr_len += (src_mode == 0x03u) ? 8u : 2u;
	}

	return (hdr_len <= psdu_len) ? hdr_len : 0u;
}

static inline bool tlsr8258_core_psdu_is_ack_for_seq(const uint8_t *psdu, uint8_t psdu_len,
						      uint8_t seq)
{
	if ((psdu == NULL) || (psdu_len < 3u)) {
		return false;
	}

	return ((psdu[0] & 0x07u) == 0x02u) && (psdu[2] == seq);
}

static inline bool tlsr8258_core_assoc_resp_to_ieee(const uint8_t *psdu, uint8_t psdu_len,
						    const struct tlsr8258_core_filter_ctx *filter)
{
	uint16_t fcf;
	uint8_t hdr_len;

	if ((psdu == NULL) || (filter == NULL) || (filter->ieee_addr == NULL)) {
		return false;
	}

	if ((psdu_len < 13u) || ((psdu[0] & 0x07u) != 0x03u) || ((psdu[1] & 0x0cu) != 0x0cu)) {
		return false;
	}

	if (memcmp(&psdu[5], filter->ieee_addr, 8u) != 0) {
		return false;
	}

	fcf = tlsr8258_core_le16(psdu);
	hdr_len = tlsr8258_core_mac_hdr_size(fcf, psdu_len);

	return (hdr_len != 0u) && ((uint16_t)(hdr_len + 1u) <= psdu_len) && (psdu[hdr_len] == 0x02u);
}

static inline bool tlsr8258_core_psdu_is_mac_command(const uint8_t *psdu, uint8_t psdu_len,
						     uint8_t cmd_id)
{
	uint16_t fcf;
	uint8_t hdr_len;

	if ((psdu == NULL) || (psdu_len < 4u)) {
		return false;
	}

	fcf = tlsr8258_core_le16(psdu);
	if ((fcf & 0x0007u) != 0x03u) {
		return false;
	}

	hdr_len = tlsr8258_core_mac_hdr_size(fcf, psdu_len);
	if ((hdr_len == 0u) || (hdr_len >= psdu_len)) {
		return false;
	}

	return psdu[hdr_len] == cmd_id;
}

static inline bool tlsr8258_core_psdu_expects_post_tx_followup(const uint8_t *psdu,
							       uint8_t psdu_len)
{
	return tlsr8258_core_psdu_is_mac_command(psdu, psdu_len, 0x04u) ||
	       tlsr8258_core_psdu_is_mac_command(psdu, psdu_len, 0x07u);
}

/*
 * True iff the frame's source address equals our own (filter) short/IEEE
 * address. Port of the driver's tlsr8258_psdu_src_matches_local so the ISR's
 * self-originated test can be exercised on host. filter->short_addr /
 * ieee_addr are little-endian, as stored in the radio filter.
 */
static inline bool tlsr8258_core_psdu_src_matches_filter(
	const uint8_t *psdu, uint8_t psdu_len, const struct tlsr8258_core_filter_ctx *filter)
{
	uint16_t fcf;
	uint8_t idx = 3u;
	uint8_t dst_mode;
	uint8_t src_mode;

	if ((psdu == NULL) || (filter == NULL) || (filter->short_addr == NULL) ||
	    (filter->ieee_addr == NULL) || (psdu_len < idx)) {
		return false;
	}

	fcf = tlsr8258_core_le16(psdu);
	dst_mode = (uint8_t)((fcf >> 10) & 0x03u);
	src_mode = (uint8_t)((fcf >> 14) & 0x03u);

	if (dst_mode != 0u) {
		idx += 2u; /* dst PAN id */
		idx += (dst_mode == 0x03u) ? 8u : 2u;
	}
	if (src_mode == 0u) {
		return false;
	}
	if ((fcf & BIT(6)) == 0u) {
		idx += 2u; /* src PAN id (no intra-PAN compression) */
	}
	if (src_mode == 0x02u) {
		if ((uint16_t)(idx + 2u) > psdu_len) {
			return false;
		}
		return memcmp(&psdu[idx], filter->short_addr, 2u) == 0;
	}
	if (src_mode == 0x03u) {
		if ((uint16_t)(idx + 8u) > psdu_len) {
			return false;
		}
		return memcmp(&psdu[idx], filter->ieee_addr, 8u) == 0;
	}
	return false;
}

/*
 * True iff this is one of OUR OWN MAC command frames (data-request or beacon-
 * request) echoed back to us — the ISR must NOT auto-ACK those. Port of the
 * driver's tlsr8258_psdu_is_self_originated_command.
 */
static inline bool tlsr8258_core_psdu_is_self_originated_command(
	const uint8_t *psdu, uint8_t psdu_len, const struct tlsr8258_core_filter_ctx *filter)
{
	uint16_t fcf;

	if ((psdu == NULL) || (filter == NULL) || (psdu_len < 4u)) {
		return false;
	}
	fcf = tlsr8258_core_le16(psdu);
	if ((fcf & 0x0007u) != 0x03u) { /* MAC command frame type */
		return false;
	}
	if (!tlsr8258_core_psdu_src_matches_filter(psdu, psdu_len, filter)) {
		return false;
	}
	return tlsr8258_core_psdu_is_mac_command(psdu, psdu_len, 0x04u) ||
	       tlsr8258_core_psdu_is_mac_command(psdu, psdu_len, 0x07u);
}

struct tlsr8258_core_rx_ack_decision {
	bool self_originated; /* our own data/beacon-req echo -> do NOT ack */
	bool ack_requested;   /* !self_originated && FCF ack-request bit set */
	bool filter_match;    /* dst is us / broadcast -> we are allowed to ack */
	bool should_ack;      /* ack_requested && filter_match: ISR sends a MAC ACK */
};

/*
 * The exact decision the RX ISR (rx_capture_common) makes for a received PSDU:
 * whether it is self-originated, whether it wants an ACK, whether it is
 * addressed to us, and hence whether we transmit a MAC ACK. Extracted so the
 * driver and the host unit test share ONE implementation (no drift).
 */
static inline void tlsr8258_core_rx_ack_decision(
	const uint8_t *psdu, uint8_t psdu_len, const struct tlsr8258_core_filter_ctx *filter,
	struct tlsr8258_core_rx_ack_decision *out)
{
	if (out == NULL) {
		return;
	}
	*out = (struct tlsr8258_core_rx_ack_decision){ 0 };
	if ((psdu == NULL) || (filter == NULL)) {
		return;
	}
	out->self_originated =
		tlsr8258_core_psdu_is_self_originated_command(psdu, psdu_len, filter);
	out->ack_requested =
		!out->self_originated && tlsr8258_ackf_ack_requested(psdu, psdu_len);
	out->filter_match = tlsr8258_ackf_dst_matches_filter(
		psdu, filter->pan_id, filter->short_addr, filter->ieee_addr);
	out->should_ack = out->ack_requested && out->filter_match;
}

static inline void tlsr8258_core_handle_tx_done(uint16_t irq_status, bool has_rx,
						bool ack_tx_pending,
						bool expect_post_tx_rx,
						bool op_state_is_tx_pending,
						struct tlsr8258_core_tx_done_result *result)
{
	if (result == NULL) {
		return;
	}

	result->tx_success = tlsr8258_tx_irq_indicates_success(irq_status);
	result->tx_had_rx = has_rx;
	result->enter_rx_fast = result->tx_success && !has_rx;
	result->rearm_rx_buffer = result->enter_rx_fast;
	result->clear_ack_tx_pending = ack_tx_pending;
	result->count_ack_tx_completion = ack_tx_pending;
	result->defer_stack_tx_to_rx_path = result->tx_success && !ack_tx_pending &&
					       expect_post_tx_rx && !has_rx;
	result->complete_stack_tx = result->tx_success && !ack_tx_pending &&
				    !result->defer_stack_tx_to_rx_path &&
				    op_state_is_tx_pending;
}

static inline void tlsr8258_core_handle_rx_frame(const uint8_t *psdu, uint8_t psdu_len,
						 uint8_t tx_seq,
						 const struct tlsr8258_core_filter_ctx *filter,
						 struct tlsr8258_core_rx_result *result)
{
	if (result == NULL) {
		return;
	}

	*result = (struct tlsr8258_core_rx_result){ 0 };
	if ((psdu == NULL) || (filter == NULL) || (filter->pan_id == NULL) ||
	    (filter->short_addr == NULL) || (filter->ieee_addr == NULL)) {
		return;
	}

	result->ack_requested = tlsr8258_ackf_ack_requested(psdu, psdu_len);
	result->ack_eligible = result->ack_requested &&
			       tlsr8258_ackf_dst_matches_filter(psdu, filter->pan_id,
								filter->short_addr,
								filter->ieee_addr);
	result->is_ack = tlsr8258_core_psdu_is_ack_for_seq(psdu, psdu_len, tx_seq);
	result->assoc_resp_to_ieee =
		tlsr8258_core_assoc_resp_to_ieee(psdu, psdu_len, filter);
	result->ack_seq = (psdu_len >= 3u) ? psdu[2] : 0u;

	if (result->is_ack) {
		result->ack_pending = (psdu[0] & BIT(4)) != 0u;
	}

	result->is_pending_response = !result->is_ack && result->ack_eligible;
}

static inline void tlsr8258_core_handle_rx_dma(uint8_t *rx_active, uint8_t *buf_a,
						uint8_t *buf_b,
						struct tlsr8258_core_rx_dma_result *result)
{
	if (result == NULL) {
		return;
	}

	*result = (struct tlsr8258_core_rx_dma_result){
		.rx_proc = rx_active,
		.next_rx_active = tlsr8258_core_next_rx_buffer(rx_active, buf_a, buf_b),
		.rearm_rx_buffer = true,
	};
}

static inline void tlsr8258_core_handle_rx_only_tx_completion(
	bool has_tx, bool op_state_is_tx_pending,
	struct tlsr8258_core_rx_only_tx_result *result)
{
	if (result == NULL) {
		return;
	}

	result->saw_rx_while_tx_pending = !has_tx && op_state_is_tx_pending;
	result->complete_stack_tx = result->saw_rx_while_tx_pending;
}

static inline void tlsr8258_core_observe_post_tx_rx_window(
	bool window_armed, bool has_tx, bool has_rx, uint16_t raw_irq,
	uint32_t gap_us, uint32_t window_us,
	struct tlsr8258_core_post_tx_rx_window_result *result)
{
	bool saw_raw_rx;

	if (result == NULL) {
		return;
	}

	*result = (struct tlsr8258_core_post_tx_rx_window_result){
		.window_armed_after = window_armed,
		.classification = TLSR8258_CORE_POST_TX_RX_NO_EVENT,
	};
	if (!window_armed || has_tx) {
		return;
	}

	if (gap_us >= window_us) {
		result->window_armed_after = false;
		result->timed_out_without_raw_rx = true;
		result->classification = TLSR8258_CORE_POST_TX_RX_NO_RAW_TIMEOUT;
		return;
	}

	saw_raw_rx = (raw_irq & (BIT(0) | BIT(4) | BIT(9))) != 0u;
	if (saw_raw_rx) {
		result->window_armed_after = false;
		result->saw_raw_rx = true;
		result->saw_valid_rx_handoff = has_rx;
		result->classification = has_rx ?
			TLSR8258_CORE_POST_TX_RX_RX_HANDOFF :
			TLSR8258_CORE_POST_TX_RX_RAW_DROPPED;
		return;
	}
}

#endif
