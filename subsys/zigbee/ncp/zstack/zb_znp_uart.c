/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ZNP UART transport: SOF 0xFE, length, two command bytes, payload and an
 * XOR checksum over everything between the SOF and the checksum. The UART is
 * polled from the Zigbee thread, as for ZBHCI, so the handlers run on that
 * thread and call the stack directly.
 */
#include <errno.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_znp.h>

#include "zb_znp_internal.h"

LOG_MODULE_REGISTER(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

/* Outgoing bytes leave a few at a time from the poll; see tx_drain(). */
#define ZNP_TX_RING_SIZE   1024U
#define ZNP_TX_BURST_BYTES 32U

static const struct device *znp_uart_dev;

static void tx_drain(size_t max);

static struct znp_parser rx;

#ifdef CONFIG_ZIGBEE_NCP_ZSTACK_UART_IRQ
#define ZNP_RX_RING_SIZE 512U

/* Filled by the UART interrupt, drained by zb_znp_poll(); a byte that does not fit is dropped */
static uint8_t rx_ring[ZNP_RX_RING_SIZE];
static volatile size_t rx_head;
static volatile size_t rx_tail;

static void znp_uart_isr(const struct device *dev, void *user_data)
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
			size_t next = (rx_head + 1U) % ZNP_RX_RING_SIZE;

			if (next != rx_tail) {
				rx_ring[rx_head] = buf[i];
				rx_head = next;
			}
		}
		uart_irq_update(dev);
	}
}
#endif

static void znp_dispatch(uint8_t cmd0, uint8_t cmd1, const uint8_t *data, uint8_t len)
{
	uint8_t type = cmd0 >> 5;
	uint8_t subsys = cmd0 & 0x1fU;
	uint8_t err[3];

	if (type != ZNP_TYPE_SREQ && type != ZNP_TYPE_AREQ) {
		return;
	}

	if (znp_sys_handle(subsys, cmd1, data, len) ||
	    znp_tables_handle(subsys, cmd1, data, len) ||
	    znp_zdo_handle(subsys, cmd1, data, len) || znp_af_handle(subsys, cmd1, data, len)) {
		return;
	}

	LOG_WRN("znp: unhandled type %u subsys 0x%02x cmd 0x%02x", type, subsys, cmd1);

	/* An AREQ has no response; an SREQ must be answered. */
	if (type == ZNP_TYPE_SREQ) {
		err[0] = ZNP_RPC_INVALID_COMMAND;
		err[1] = cmd0;
		err[2] = cmd1;
		znp_srsp(ZNP_SUBSYS_RPC_ERR, 0x00U, err, sizeof(err));
	}
}

static void znp_rx_byte(uint8_t byte)
{
	switch (znp_parser_feed(&rx, byte)) {
	case ZNP_FEED_FRAME:
		znp_dispatch(rx.cmd0, rx.cmd1, rx.data, rx.len);
		break;
	case ZNP_FEED_BAD_FCS:
		LOG_WRN("znp: bad checksum 0x%02x, expected 0x%02x", byte, rx.fcs);
		break;
	case ZNP_FEED_MORE:
		break;
	}
}

int zb_znp_init(const struct device *uart_dev)
{
	if (uart_dev == NULL || !device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	znp_uart_dev = uart_dev;
	znp_parser_reset(&rx);
	znp_sys_start();

#ifdef CONFIG_ZIGBEE_NCP_ZSTACK_UART_IRQ
	uart_irq_callback_user_data_set(uart_dev, znp_uart_isr, NULL);
	uart_irq_rx_enable(uart_dev);
#endif

	return 0;
}

void zb_znp_poll(void)
{
	unsigned char byte;

	if (znp_uart_dev == NULL) {
		return;
	}

#ifdef CONFIG_ZIGBEE_NCP_ZSTACK_UART_IRQ
	while (rx_tail != rx_head) {
		byte = rx_ring[rx_tail];
		rx_tail = (rx_tail + 1U) % ZNP_RX_RING_SIZE;
		znp_rx_byte((uint8_t)byte);
	}
#else
	while (uart_poll_in(znp_uart_dev, &byte) == 0) {
		znp_rx_byte((uint8_t)byte);
	}
#endif

	tx_drain(ZNP_TX_BURST_BYTES);
	znp_persist_service();
}

/*
 * Outgoing bytes wait in a ring and leave a few at a time from the poll, so
 * that sending a long frame at a low baud rate does not keep the Zigbee
 * thread from the radio for the whole frame.
 */
static uint8_t tx_ring[ZNP_TX_RING_SIZE];
static size_t tx_head;
static size_t tx_tail;

static size_t tx_pending(void)
{
	return (tx_head - tx_tail) % ZNP_TX_RING_SIZE;
}

static void tx_drain(size_t max)
{
	while (max-- > 0U && tx_head != tx_tail) {
		uart_poll_out(znp_uart_dev, tx_ring[tx_tail]);
		tx_tail = (tx_tail + 1U) % ZNP_TX_RING_SIZE;
	}
}

static void tx_put(uint8_t byte)
{
	/* One slot stays free to tell a full ring from an empty one. */
	while (tx_pending() == ZNP_TX_RING_SIZE - 1U) {
		tx_drain(1U);
	}

	tx_ring[tx_head] = byte;
	tx_head = (tx_head + 1U) % ZNP_TX_RING_SIZE;
}

void znp_send(uint8_t type, uint8_t subsys, uint8_t cmd, const uint8_t *data, size_t len)
{
	uint8_t frame[ZNP_MAX_DATA + 5U];
	size_t n;

	if (znp_uart_dev == NULL || len > ZNP_MAX_DATA) {
		return;
	}

	n = znp_frame_encode(frame, type, subsys, cmd, data, len);
	for (size_t i = 0; i < n; i++) {
		tx_put(frame[i]);
	}
}
