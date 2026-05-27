/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_tlsr8258_radio_op.h"

void tlsr8258_radio_op_reset(struct tlsr8258_radio_op *op)
{
	*op = (struct tlsr8258_radio_op){ 0 };
	op->state = TLSR8258_RADIO_OP_IDLE;
}

void tlsr8258_radio_op_prepare_tx(struct tlsr8258_radio_op *op, uint8_t tx_seq,
				  bool expect_ack, bool expect_post_tx_rx)
{
	tlsr8258_radio_op_reset(op);
	op->state = TLSR8258_RADIO_OP_TX_PENDING;
	op->tx_seq = tx_seq;
	op->expect_ack = expect_ack;
	op->expect_post_tx_rx = expect_post_tx_rx;
}

bool tlsr8258_radio_op_on_tx_success(struct tlsr8258_radio_op *op)
{
	if (!op->expect_post_tx_rx) {
		op->state = TLSR8258_RADIO_OP_COMPLETE_OK;
		op->result_errno = 0;
		return true;
	}

	op->state = TLSR8258_RADIO_OP_WAITING_POST_TX_RX;
	return false;
}

void tlsr8258_radio_op_on_tx_error(struct tlsr8258_radio_op *op, int result_errno)
{
	op->state = TLSR8258_RADIO_OP_COMPLETE_ERROR;
	op->result_errno = result_errno;
}

void tlsr8258_radio_op_on_timeout(struct tlsr8258_radio_op *op)
{
	op->state = TLSR8258_RADIO_OP_COMPLETE_NO_RX;
	op->result_errno = -11;
}

bool tlsr8258_radio_op_on_rx(struct tlsr8258_radio_op *op, bool is_ack,
			     bool ack_pending, bool is_pending_response)
{
	if (op->state != TLSR8258_RADIO_OP_WAITING_POST_TX_RX) {
		return false;
	}

	if (is_ack) {
		op->ack_seen = true;
		op->ack_pending = ack_pending;
		if (!ack_pending) {
			op->state = TLSR8258_RADIO_OP_COMPLETE_OK;
			op->result_errno = 0;
			return true;
		}

		return false;
	}

	if (is_pending_response) {
		op->state = TLSR8258_RADIO_OP_COMPLETE_OK;
		op->result_errno = 0;
		return true;
	}

	return false;
}

int tlsr8258_radio_op_result_errno(const struct tlsr8258_radio_op *op)
{
	return op->result_errno;
}
