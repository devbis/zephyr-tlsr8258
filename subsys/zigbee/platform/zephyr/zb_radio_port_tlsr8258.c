/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The part of the radio port that the generic port cannot provide on the
 * TLSR8258: the Frame Pending bit of an ACK, which the radio receives after
 * tx() has returned. The MAC clock is in zb_radio_port_tlsr82xx.c, everything
 * else in zb_radio_port_generic.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/ieee802154/tlsr8258.h>
#include <zephyr/zigbee/zb_radio_port.h>

bool zb_radio_port_ack_frame_pending_get(uint8_t seq, bool *pending)
{
	return tlsr8258_radio_ack_frame_pending_get(seq, pending);
}
