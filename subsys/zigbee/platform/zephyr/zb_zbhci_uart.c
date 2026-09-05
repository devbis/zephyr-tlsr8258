/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ZBHCI UART transport: byte-stream framing/CRC8 adapted from Telink's
 * tl_zigbee_sdk zbhci/uart/hci_uart.c (Copyright (c) 2021 Telink
 * Semiconductor (Shanghai) Co., Ltd., Apache-2.0), reworked as an
 * incremental byte-at-a-time parser fed from Zephyr's interrupt-driven UART
 * API instead of the vendor's single-shot "whole buffer" driver callback.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_zbhci.h>

LOG_MODULE_REGISTER(zigbee_zbhci_uart, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZBHCI_MAX_PAYLOAD 128U
#define ZBHCI_HDR_LEN      5U /* msgType(2) + msgLen(2) + crc8(1), after the start byte */

enum zbhci_rx_state {
	ZBHCI_RX_WAIT_START = 0,
	ZBHCI_RX_HDR,
	ZBHCI_RX_PAYLOAD,
	ZBHCI_RX_WAIT_END,
};

struct zbhci_rx_parser {
	enum zbhci_rx_state state;
	uint8_t hdr[ZBHCI_HDR_LEN];
	size_t hdr_pos;
	uint16_t msg_type;
	uint16_t msg_len;
	uint8_t payload[ZBHCI_MAX_PAYLOAD];
	size_t payload_pos;
};

static const struct device *zbhci_uart_dev;
static struct zbhci_rx_parser rx_parser;

static uint8_t zbhci_crc8(uint16_t type, uint16_t len, const uint8_t *data)
{
	uint8_t crc = (uint8_t)(type & 0xffU);

	crc ^= (uint8_t)(type >> 8);
	crc ^= (uint8_t)(len & 0xffU);
	crc ^= (uint8_t)(len >> 8);
	for (uint16_t i = 0; i < len; i++) {
		crc ^= data[i];
	}

	return crc;
}

static void zbhci_rx_reset(struct zbhci_rx_parser *p)
{
	p->state = ZBHCI_RX_WAIT_START;
	p->hdr_pos = 0;
	p->payload_pos = 0;
}

static void zbhci_rx_feed_byte(struct zbhci_rx_parser *p, uint8_t byte)
{
	switch (p->state) {
	case ZBHCI_RX_WAIT_START:
		if (byte == ZBHCI_MSG_START_FLAG) {
			p->hdr_pos = 0;
			p->state = ZBHCI_RX_HDR;
		}
		break;

	case ZBHCI_RX_HDR:
		p->hdr[p->hdr_pos++] = byte;
		if (p->hdr_pos == sizeof(p->hdr)) {
			p->msg_type = ((uint16_t)p->hdr[0] << 8) | p->hdr[1];
			p->msg_len = ((uint16_t)p->hdr[2] << 8) | p->hdr[3];
			/*
			 * hdr[4] is the CRC8 byte. The vendor transport only
			 * enforces it for OTA start/block messages, which are
			 * out of scope for the command subset implemented
			 * here (see zb_zbhci_cmd.c); every other command
			 * accepts the frame unconditionally, matching vendor
			 * behavior.
			 */
			if (p->msg_len > sizeof(p->payload)) {
				LOG_WRN("zbhci: oversized frame len=%u, resyncing",
					p->msg_len);
				zbhci_rx_reset(p);
				break;
			}
			p->payload_pos = 0;
			p->state = (p->msg_len == 0U) ? ZBHCI_RX_WAIT_END : ZBHCI_RX_PAYLOAD;
		}
		break;

	case ZBHCI_RX_PAYLOAD:
		p->payload[p->payload_pos++] = byte;
		if (p->payload_pos == p->msg_len) {
			p->state = ZBHCI_RX_WAIT_END;
		}
		break;

	case ZBHCI_RX_WAIT_END:
		if (byte == ZBHCI_MSG_END_FLAG) {
			/*
			 * Called directly (not deferred via tl_zbTaskPost):
			 * zb_zbhci_uart_poll() -- and therefore this whole
			 * byte-feed path -- only ever runs from the Zigbee
			 * thread's own main loop (see zb_main.c), never from
			 * an ISR, so it is already safe to touch the ZDO/AF/
			 * BDB APIs here directly.
			 */
			zb_zbhci_cmd_handle(p->msg_type, p->msg_len, p->payload);
		} else {
			LOG_WRN("zbhci: bad end byte 0x%02x, dropping frame", byte);
		}
		/* Resynchronize on the next start byte either way. */
		zbhci_rx_reset(p);
		break;
	}
}

int zb_zbhci_uart_init(const struct device *uart_dev)
{
	if (uart_dev == NULL || !device_is_ready(uart_dev)) {
		return -ENODEV;
	}

	zbhci_uart_dev = uart_dev;
	zbhci_rx_reset(&rx_parser);

	return 0;
}

void zb_zbhci_uart_poll(void)
{
	unsigned char byte;

	/*
	 * Polled from the Zigbee thread's own loop (zb_main.c), the same
	 * pattern zb_platform_radio_rx_poll() uses: that thread never yields
	 * (see the comment on its while(1) loop), so Zephyr's async
	 * interrupt-driven UART API -- serviced by a separate thread on
	 * native_sim -- would starve. uart_poll_in() is a plain non-blocking
	 * read and needs no interrupt/callback machinery.
	 */
	if (zbhci_uart_dev == NULL) {
		return;
	}

	while (uart_poll_in(zbhci_uart_dev, &byte) == 0) {
		zbhci_rx_feed_byte(&rx_parser, (uint8_t)byte);
	}
}

void zb_zbhci_send(uint16_t msg_type, uint16_t len, const uint8_t *payload)
{
	uint8_t crc;

	if (zbhci_uart_dev == NULL) {
		return;
	}

	crc = zbhci_crc8(msg_type, len, payload);

	uart_poll_out(zbhci_uart_dev, ZBHCI_MSG_START_FLAG);
	uart_poll_out(zbhci_uart_dev, (uint8_t)(msg_type >> 8));
	uart_poll_out(zbhci_uart_dev, (uint8_t)(msg_type & 0xffU));
	uart_poll_out(zbhci_uart_dev, (uint8_t)(len >> 8));
	uart_poll_out(zbhci_uart_dev, (uint8_t)(len & 0xffU));
	uart_poll_out(zbhci_uart_dev, crc);
	for (uint16_t i = 0; i < len; i++) {
		uart_poll_out(zbhci_uart_dev, payload[i]);
	}
	uart_poll_out(zbhci_uart_dev, ZBHCI_MSG_END_FLAG);
}
