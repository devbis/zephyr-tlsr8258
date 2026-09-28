/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/ztest.h>

#include "ieee802154_tlsr8258_radio_op.h"




ZTEST(tlsr8258_radio_op, test_tx_success_with_post_rx_enters_waiting_state)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);

	zassert_true(tlsr8258_radio_op_on_tx_success(&op) == false);
	zassert_equal(op.state, TLSR8258_RADIO_OP_WAITING_POST_TX_RX);
}

ZTEST(tlsr8258_radio_op, test_tx_success_without_post_rx_completes_immediately)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, false, false);

	zassert_true(tlsr8258_radio_op_on_tx_success(&op));
	zassert_equal(op.state, TLSR8258_RADIO_OP_COMPLETE_OK);
	zassert_equal(tlsr8258_radio_op_result_errno(&op), 0);
}

ZTEST(tlsr8258_radio_op, test_ack_without_pending_completes_operation)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);
	(void)tlsr8258_radio_op_on_tx_success(&op);

	zassert_true(tlsr8258_radio_op_on_rx(&op, true, false, false));
	zassert_equal(tlsr8258_radio_op_result_errno(&op), 0);
}

ZTEST(tlsr8258_radio_op, test_ack_with_pending_waits_for_follow_up_response)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);
	(void)tlsr8258_radio_op_on_tx_success(&op);

	zassert_true(!tlsr8258_radio_op_on_rx(&op, true, true, false));
	zassert_equal(op.state, TLSR8258_RADIO_OP_WAITING_POST_TX_RX);
	zassert_true(op.ack_seen);
	zassert_true(op.ack_pending);
	zassert_true(tlsr8258_radio_op_on_rx(&op, false, false, true));
	zassert_equal(op.state, TLSR8258_RADIO_OP_COMPLETE_OK);
	zassert_equal(tlsr8258_radio_op_result_errno(&op), 0);
}

ZTEST(tlsr8258_radio_op, test_tx_error_completes_with_supplied_errno)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);
	tlsr8258_radio_op_on_tx_error(&op, -EIO);

	zassert_equal(op.state, TLSR8258_RADIO_OP_COMPLETE_ERROR);
	zassert_equal(tlsr8258_radio_op_result_errno(&op), -EIO);
}

ZTEST(tlsr8258_radio_op, test_timeout_maps_to_eagain)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, false, true);
	(void)tlsr8258_radio_op_on_tx_success(&op);
	tlsr8258_radio_op_on_timeout(&op);

	zassert_equal(op.state, TLSR8258_RADIO_OP_COMPLETE_NO_RX);
	zassert_equal(tlsr8258_radio_op_result_errno(&op), -EAGAIN);
}

ZTEST_SUITE(tlsr8258_radio_op, NULL, NULL, NULL, NULL, NULL);
