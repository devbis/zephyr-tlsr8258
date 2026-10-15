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

enum znp_rx_state {
	ZNP_RX_SOF,
	ZNP_RX_LEN,
	ZNP_RX_CMD0,
	ZNP_RX_CMD1,
	ZNP_RX_DATA,
	ZNP_RX_FCS,
};

static const struct device *znp_uart_dev;

static struct {
	enum znp_rx_state state;
	uint8_t len;
	uint8_t cmd0;
	uint8_t cmd1;
	uint8_t pos;
	uint8_t fcs;
	uint8_t data[ZNP_MAX_DATA];
} rx;

static void znp_dispatch(uint8_t cmd0, uint8_t cmd1, const uint8_t *data, uint8_t len)
{
	uint8_t type = cmd0 >> 5;
	uint8_t subsys = cmd0 & 0x1fU;
	uint8_t err[3];

	if (type != ZNP_TYPE_SREQ && type != ZNP_TYPE_AREQ) {
		return;
	}

	if (znp_sys_handle(subsys, cmd1, data, len) || znp_zdo_handle(subsys, cmd1, data, len) ||
	    znp_af_handle(subsys, cmd1, data, len)) {
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
	switch (rx.state) {
	case ZNP_RX_SOF:
		/* Anything else, such as the 0xEF bootloader skip bytes, is noise. */
		if (byte == ZNP_SOF) {
			rx.state = ZNP_RX_LEN;
		}
		break;
	case ZNP_RX_LEN:
		if (byte > ZNP_MAX_DATA) {
			rx.state = ZNP_RX_SOF;
			break;
		}
		rx.len = byte;
		rx.fcs = byte;
		rx.state = ZNP_RX_CMD0;
		break;
	case ZNP_RX_CMD0:
		rx.cmd0 = byte;
		rx.fcs ^= byte;
		rx.state = ZNP_RX_CMD1;
		break;
	case ZNP_RX_CMD1:
		rx.cmd1 = byte;
		rx.fcs ^= byte;
		rx.pos = 0U;
		rx.state = (rx.len > 0U) ? ZNP_RX_DATA : ZNP_RX_FCS;
		break;
	case ZNP_RX_DATA:
		rx.data[rx.pos++] = byte;
		rx.fcs ^= byte;
		if (rx.pos == rx.len) {
			rx.state = ZNP_RX_FCS;
		}
		break;
	case ZNP_RX_FCS:
		rx.state = ZNP_RX_SOF;
		if (byte == rx.fcs) {
			znp_dispatch(rx.cmd0, rx.cmd1, rx.data, rx.len);
		} else {
			LOG_WRN("znp: bad checksum 0x%02x, expected 0x%02x", byte, rx.fcs);
		}
		break;
	}
}

int zb_znp_init(const struct device *uart_dev)
{
	if (uart_dev == NULL || !device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	znp_uart_dev = uart_dev;
	rx.state = ZNP_RX_SOF;
	znp_sys_start();

	return 0;
}

void zb_znp_poll(void)
{
	unsigned char byte;

	if (znp_uart_dev == NULL) {
		return;
	}

	while (uart_poll_in(znp_uart_dev, &byte) == 0) {
		znp_rx_byte((uint8_t)byte);
	}
}

void znp_send(uint8_t type, uint8_t subsys, uint8_t cmd, const uint8_t *data, size_t len)
{
	uint8_t cmd0 = (uint8_t)((type << 5) | (subsys & 0x1fU));
	uint8_t fcs = (uint8_t)len ^ cmd0 ^ cmd;

	if (znp_uart_dev == NULL || len > ZNP_MAX_DATA) {
		return;
	}

	uart_poll_out(znp_uart_dev, ZNP_SOF);
	uart_poll_out(znp_uart_dev, (uint8_t)len);
	uart_poll_out(znp_uart_dev, cmd0);
	uart_poll_out(znp_uart_dev, cmd);
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(znp_uart_dev, data[i]);
		fcs ^= data[i];
	}
	uart_poll_out(znp_uart_dev, fcs);
}
