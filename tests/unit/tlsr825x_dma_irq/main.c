/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/ztest.h>

#include <dma_irq.h>

#define CHN_UART_RX BIT(0)
#define CHN_UART_TX BIT(1)
#define CHN_RF_RX   BIT(2)
#define CHN_RF_TX   BIT(3)

static const uint8_t masks[] = {CHN_UART_RX | CHN_UART_TX, CHN_RF_RX | CHN_RF_TX};

ZTEST(tlsr825x_dma_irq, test_uart_status_selects_only_the_uart)
{
	zassert_equal(tlsr825x_dma_irq_select(masks, 2, CHN_UART_RX), BIT(0));
	zassert_equal(tlsr825x_dma_irq_select(masks, 2, CHN_UART_TX), BIT(0));
}

ZTEST(tlsr825x_dma_irq, test_radio_status_selects_only_the_radio)
{
	zassert_equal(tlsr825x_dma_irq_select(masks, 2, CHN_RF_RX), BIT(1));
	zassert_equal(tlsr825x_dma_irq_select(masks, 2, CHN_RF_TX), BIT(1));
}

ZTEST(tlsr825x_dma_irq, test_both_pending_selects_both)
{
	zassert_equal(tlsr825x_dma_irq_select(masks, 2, CHN_UART_RX | CHN_RF_RX), BIT(0) | BIT(1));
}

ZTEST(tlsr825x_dma_irq, test_unserved_channel_selects_nothing)
{
	zassert_equal(tlsr825x_dma_irq_select(masks, 2, BIT(4)), 0);
	zassert_equal(tlsr825x_dma_irq_select(masks, 2, 0), 0);
	zassert_equal(tlsr825x_dma_irq_select(masks, 0, 0xff), 0);
}

ZTEST_SUITE(tlsr825x_dma_irq, NULL, NULL, NULL, NULL, NULL);
