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
