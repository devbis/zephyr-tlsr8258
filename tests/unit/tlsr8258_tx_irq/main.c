/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "ieee802154_tlsr8258_tx_irq.h"

#define RF_IRQ_TX          BIT(1)
#define RF_IRQ_CMD_DONE    BIT(5)
#define RF_IRQ_FSM_TIMEOUT BIT(6)
#define RF_IRQ_TX_DS       BIT(8)
#define RF_IRQ_STX_TIMEOUT BIT(12)

ZTEST(tlsr8258_tx_irq, test_cmd_done_alone_is_not_success)
{
	zassert_false(tlsr8258_tx_irq_indicates_success(RF_IRQ_CMD_DONE));
}

ZTEST(tlsr8258_tx_irq, test_tx_irq_is_success)
{
	zassert_true(tlsr8258_tx_irq_indicates_success(RF_IRQ_TX));
}

ZTEST(tlsr8258_tx_irq, test_tx_ds_irq_is_success)
{
	zassert_true(tlsr8258_tx_irq_indicates_success(RF_IRQ_TX_DS));
}

ZTEST(tlsr8258_tx_irq, test_timeout_irqs_are_not_success)
{
	zassert_false(tlsr8258_tx_irq_indicates_success(RF_IRQ_STX_TIMEOUT));
	zassert_false(tlsr8258_tx_irq_indicates_success(RF_IRQ_FSM_TIMEOUT));
	zassert_false(tlsr8258_tx_irq_indicates_success(RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT));
}

ZTEST(tlsr8258_tx_irq, test_session_mask_keeps_tx_ds_for_regular_tx)
{
	uint16_t mask = RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT;

	zassert_true(tlsr8258_tx_irq_session_mask(mask, false) == mask);
}

ZTEST(tlsr8258_tx_irq, test_session_mask_drops_tx_ds_for_post_tx_followup_candidate)
{
	uint16_t mask = RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT;

	zassert_true(tlsr8258_tx_irq_session_mask(mask, true) ==
		     (uint16_t)(RF_IRQ_TX | RF_IRQ_STX_TIMEOUT));
}

ZTEST(tlsr8258_tx_irq, test_start_clear_mask_keeps_full_clear_for_regular_tx)
{
	zassert_true(tlsr8258_tx_irq_start_clear_mask(false) == 0xffffu);
}

ZTEST(tlsr8258_tx_irq, test_start_clear_mask_matches_vendor_poll_tx_contract)
{
	zassert_true(tlsr8258_tx_irq_start_clear_mask(true) == (uint16_t)(BIT(0) | BIT(1)));
}

ZTEST(tlsr8258_tx_irq, test_force_manual_off_never_stops_state_machine)
{
	/*
	 * force_manual_off used to write 0x0f00 = 0x80 (the RF-OFF command)
	 * before an association poll and never restart the state machine, so the
	 * radio was OFF for the whole post-poll window and missed the AssocResp.
	 * It must now be a no-op for BOTH cases: the poll uses the same manual TX
	 * path as every other frame.
	 */
	zassert_false(tlsr8258_tx_force_manual_off_before_start(false));
	zassert_false(tlsr8258_tx_force_manual_off_before_start(true));
}

ZTEST(tlsr8258_tx_irq, test_rx_rearm_needed_for_isrless_poll_completion)
{
	/*
	 * The association poll (followup expected) that completes outside the
	 * RF ISR must re-arm the RX DMA buffer for the AssocResp.
	 */
	zassert_true(tlsr8258_tx_poll_needs_rx_rearm(true, false));
}

ZTEST(tlsr8258_tx_irq, test_rx_rearm_not_needed_when_isr_handled_the_swap)
{
	/* If the RF ISR handled the completion it already swapped buffers. */
	zassert_false(tlsr8258_tx_poll_needs_rx_rearm(true, true));
}

ZTEST(tlsr8258_tx_irq, test_rx_rearm_not_needed_for_plain_tx)
{
	/*
	 * A regular TX (no indirect followup) never leaves the buffer occupied
	 * by an auto-received ACK in a way that blocks a follow-up frame.
	 */
	zassert_false(tlsr8258_tx_poll_needs_rx_rearm(false, false));
	zassert_false(tlsr8258_tx_poll_needs_rx_rearm(false, true));
}

ZTEST_SUITE(tlsr8258_tx_irq, NULL, NULL, NULL, NULL, NULL);
