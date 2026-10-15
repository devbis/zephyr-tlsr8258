/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ZNP framing: SOF 0xFE, length, two command bytes, payload and an XOR
 * checksum over everything between the SOF and the checksum.
 */
#include "zb_znp_internal.h"

void znp_parser_reset(struct znp_parser *p)
{
	p->state = ZNP_RX_SOF;
}

enum znp_feed_result znp_parser_feed(struct znp_parser *p, uint8_t byte)
{
	switch (p->state) {
	case ZNP_RX_SOF:
		/* Anything else, such as the 0xEF bootloader skip bytes, is noise. */
		if (byte == ZNP_SOF) {
			p->state = ZNP_RX_LEN;
		}
		break;
	case ZNP_RX_LEN:
		if (byte > ZNP_MAX_DATA) {
			p->state = ZNP_RX_SOF;
			break;
		}
		p->len = byte;
		p->fcs = byte;
		p->state = ZNP_RX_CMD0;
		break;
	case ZNP_RX_CMD0:
		p->cmd0 = byte;
		p->fcs ^= byte;
		p->state = ZNP_RX_CMD1;
		break;
	case ZNP_RX_CMD1:
		p->cmd1 = byte;
		p->fcs ^= byte;
		p->pos = 0U;
		p->state = (p->len > 0U) ? ZNP_RX_DATA : ZNP_RX_FCS;
		break;
	case ZNP_RX_DATA:
		p->data[p->pos++] = byte;
		p->fcs ^= byte;
		if (p->pos == p->len) {
			p->state = ZNP_RX_FCS;
		}
		break;
	case ZNP_RX_FCS:
		p->state = ZNP_RX_SOF;
		return (byte == p->fcs) ? ZNP_FEED_FRAME : ZNP_FEED_BAD_FCS;
	}

	return ZNP_FEED_MORE;
}

size_t znp_frame_encode(uint8_t *out, uint8_t type, uint8_t subsys, uint8_t cmd,
			const uint8_t *data, size_t len)
{
	uint8_t cmd0 = (uint8_t)((type << 5) | (subsys & 0x1fU));
	uint8_t fcs = (uint8_t)len ^ cmd0 ^ cmd;
	size_t n = 0U;

	out[n++] = ZNP_SOF;
	out[n++] = (uint8_t)len;
	out[n++] = cmd0;
	out[n++] = cmd;
	for (size_t i = 0; i < len; i++) {
		out[n++] = data[i];
		fcs ^= data[i];
	}
	out[n++] = fcs;

	return n;
}
