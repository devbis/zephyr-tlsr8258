/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * EZSP UART transport. The UART is polled from the Zigbee thread, as for ZNP and ZBHCI, so the
 * command handlers run on that thread and call the stack directly. Outgoing bytes wait in a ring
 * and leave a few at a time from the poll.
 */
#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_ezsp.h>

#include "zb_ezsp_session.h"

LOG_MODULE_REGISTER(zigbee_ezsp, CONFIG_ZIGBEE_LOG_LEVEL);

#define EZSP_TX_RING_SIZE   1024U
#define EZSP_TX_BURST_BYTES 32U

static const struct device *ezsp_uart_dev;
static struct ezsp_session session;

static uint8_t tx_ring[EZSP_TX_RING_SIZE];
static size_t tx_head;
static size_t tx_tail;

static size_t tx_pending(void)
{
	return (tx_head - tx_tail) % EZSP_TX_RING_SIZE;
}

static void tx_drain(size_t max)
{
	while (max-- > 0U && tx_head != tx_tail) {
		uart_poll_out(ezsp_uart_dev, tx_ring[tx_tail]);
		tx_tail = (tx_tail + 1U) % EZSP_TX_RING_SIZE;
	}
}

static void session_tx(void *ctx, const uint8_t *bytes, size_t len)
{
	ARG_UNUSED(ctx);

	for (size_t i = 0; i < len; i++) {
		/* One slot stays free to tell a full ring from an empty one. */
		while (tx_pending() == EZSP_TX_RING_SIZE - 1U) {
			tx_drain(1U);
		}

		tx_ring[tx_head] = bytes[i];
		tx_head = (tx_head + 1U) % EZSP_TX_RING_SIZE;
	}
}

#ifdef CONFIG_ZIGBEE_NCP_EZSP_UART_IRQ
#define EZSP_RX_RING_SIZE 512U

/* Filled by the UART interrupt, drained by zb_ezsp_poll(); a byte that does not fit is dropped */
static uint8_t rx_ring[EZSP_RX_RING_SIZE];
static volatile size_t rx_head;
static volatile size_t rx_tail;

static void ezsp_uart_isr(const struct device *dev, void *user_data)
{
	uint8_t buf[16];

	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	/* Bounded, so an interrupt cause that is not handled here cannot hold the CPU */
	for (int pass = 0; pass < 8 && uart_irq_is_pending(dev) > 0; pass++) {
		int n;

		if (uart_irq_rx_ready(dev) <= 0) {
			/* A line error is cleared by reading the status */
			(void)uart_err_check(dev);
			uart_irq_update(dev);
			continue;
		}

		n = uart_fifo_read(dev, buf, sizeof(buf));
		for (int i = 0; i < n; i++) {
			size_t next = (rx_head + 1U) % EZSP_RX_RING_SIZE;

			if (next != rx_tail) {
				rx_ring[rx_head] = buf[i];
				rx_head = next;
			}
		}
		uart_irq_update(dev);
	}
}
#endif

int zb_ezsp_init(const struct device *uart_dev)
{
	if (uart_dev == NULL || !device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	ezsp_uart_dev = uart_dev;
	ezsp_session_init(&session, session_tx, NULL);

#ifdef CONFIG_ZIGBEE_NCP_EZSP_UART_IRQ
	uart_irq_callback_user_data_set(uart_dev, ezsp_uart_isr, NULL);
	uart_irq_rx_enable(uart_dev);
#endif

	ezsp_session_start(&session);

	return 0;
}

void zb_ezsp_poll(void)
{
	uint32_t now = k_uptime_get_32();

	if (ezsp_uart_dev == NULL) {
		return;
	}

#ifdef CONFIG_ZIGBEE_NCP_EZSP_UART_IRQ
	while (rx_tail != rx_head) {
		uint8_t byte = rx_ring[rx_tail];

		rx_tail = (rx_tail + 1U) % EZSP_RX_RING_SIZE;
		ezsp_session_rx(&session, now, byte);
	}
#else
	unsigned char byte;

	while (uart_poll_in(ezsp_uart_dev, &byte) == 0) {
		ezsp_session_rx(&session, now, (uint8_t)byte);
	}
#endif

	ezsp_session_poll(&session, now);
	tx_drain(EZSP_TX_BURST_BYTES);
}
