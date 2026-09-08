/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RADIO_OP_H_
#define ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RADIO_OP_H_

#include <stdbool.h>
#include <stdint.h>

enum tlsr8258_radio_op_state {
	TLSR8258_RADIO_OP_IDLE = 0,
	TLSR8258_RADIO_OP_TX_PENDING,
	TLSR8258_RADIO_OP_WAITING_POST_TX_RX,
	TLSR8258_RADIO_OP_COMPLETE_OK,
	TLSR8258_RADIO_OP_COMPLETE_NO_RX,
	TLSR8258_RADIO_OP_COMPLETE_ERROR,
};

struct tlsr8258_radio_op {
	enum tlsr8258_radio_op_state state;
	uint8_t tx_seq;
	bool expect_ack;
	bool expect_post_tx_rx;
	bool ack_seen;
	bool ack_pending;
	/*
	 * Set by tlsr8258_send_ack_if_needed after kicking the MAC-ACK TX
	 * from ISR context. Cleared by the same RF ISR when the next
	 * TX_DS/TX/CMD_DONE event fires. Lets the ISR exit early after
	 * kicking the ACK (the 300 µs busy-wait poll that used to live
	 * in send_ack_if_needed is gone), and tells the main TX branch
	 * to NOT treat this TX_DS as a stack-initiated TX completion.
	 */
	bool ack_tx_pending;
	int result_errno;
};

void tlsr8258_radio_op_reset(struct tlsr8258_radio_op *op);
void tlsr8258_radio_op_prepare_tx(struct tlsr8258_radio_op *op, uint8_t tx_seq,
				  bool expect_ack, bool expect_post_tx_rx);
bool tlsr8258_radio_op_on_tx_success(struct tlsr8258_radio_op *op);
void tlsr8258_radio_op_on_tx_error(struct tlsr8258_radio_op *op, int result_errno);
void tlsr8258_radio_op_on_timeout(struct tlsr8258_radio_op *op);
bool tlsr8258_radio_op_on_rx(struct tlsr8258_radio_op *op, bool is_ack,
			     bool ack_pending, bool is_pending_response);
int tlsr8258_radio_op_result_errno(const struct tlsr8258_radio_op *op);

#endif
