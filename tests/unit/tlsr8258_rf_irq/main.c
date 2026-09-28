/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/ztest.h>

#include "ieee802154_tlsr8258_rf_irq.h"

#define RF_IRQ_RX       BIT(0)
#define RF_IRQ_TX       BIT(1)
#define RF_IRQ_RX_CRC_2 BIT(4)
#define RF_IRQ_RX_DR    BIT(9)
#define RF_IRQ_RX_EVENTS (RF_IRQ_RX | RF_IRQ_RX_CRC_2 | RF_IRQ_RX_DR)

ZTEST(tlsr8258_rf_irq, test_runtime_irq_mask_matches_runtime_rx_event_contract)
{
	zassert_equal(tlsr8258_rf_irq_runtime_mask(), RF_IRQ_RX_EVENTS | RF_IRQ_TX);
}

ZTEST(tlsr8258_rf_irq, test_rx_event_accepts_all_vendor_rx_indicators)
{
	zassert_true(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX));
	zassert_true(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX | RF_IRQ_RX_CRC_2));
	zassert_true(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2));
	zassert_true(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_DR));
	zassert_true(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2 | RF_IRQ_RX_DR));
}

ZTEST(tlsr8258_rf_irq, test_zero_irq_with_valid_dma_rx_synthesizes_rx_status)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 4u;
	dma[16] = 0x10u;

	zassert_equal(tlsr8258_rf_irq_effective_status(0u, dma, sizeof(dma)), RF_IRQ_RX);
}

ZTEST(tlsr8258_rf_irq, test_secondary_rx_irq_with_valid_dma_promotes_to_logical_rx)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 4u;
	dma[16] = 0x10u;

	zassert_equal(tlsr8258_rf_irq_effective_status(RF_IRQ_RX_DR, dma, sizeof(dma)), RF_IRQ_RX);
	zassert_equal(tlsr8258_rf_irq_effective_status(RF_IRQ_RX_CRC_2, dma, sizeof(dma)),
		  RF_IRQ_RX);
}

ZTEST(tlsr8258_rf_irq, test_zero_irq_with_invalid_dma_does_not_synthesize_rx_status)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 5u;
	dma[16] = 0x00u;

	zassert_equal(tlsr8258_rf_irq_effective_status(0u, dma, sizeof(dma)), 0u);
}

ZTEST(tlsr8258_rf_irq, test_non_rx_irq_bits_do_not_trigger_rx_capture)
{
	zassert_false(tlsr8258_rf_irq_has_rx_event(RF_IRQ_TX));
	zassert_true(tlsr8258_rf_irq_has_rx_event(RF_IRQ_TX | RF_IRQ_RX_DR));
}

ZTEST(tlsr8258_rf_irq, test_invalid_dma_keeps_hardware_rx_event_for_consumer)
{
	uint8_t dma[20] = { 0 };

	/*
	 * The RF RX latch is authoritative even when DMA2 has not completed its
	 * header writeback yet. The consumer will validate/rearm the buffer.
	 */
	dma[0] = 5u;
	dma[4] = 5u;
	dma[16] = 0x00u;

	zassert_equal(tlsr8258_rf_irq_effective_status(RF_IRQ_TX | RF_IRQ_RX_DR, dma, sizeof(dma)),
		  RF_IRQ_TX | RF_IRQ_RX);
}

ZTEST_SUITE(tlsr8258_rf_irq, NULL, NULL, NULL, NULL, NULL);
