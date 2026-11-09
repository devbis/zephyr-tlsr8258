/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The parts of the radio port that the generic port cannot provide on the
 * TLSR8258: the MAC clock, the receive delivery context and the Frame Pending
 * bit of an ACK. Everything else comes from zb_radio_port_generic.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/ieee802154/tlsr8258.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_radio_port.h>

#define TLSR8258_SYSTEM_TICK_REG 0x00800740u
#define TLSR8258_SYSTEM_TICK_CYCLES_PER_US \
	(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000U)

/* MAC deadlines are in clock_time() units, the raw system tick. */
uint32_t sysTimerPerUs = TLSR8258_SYSTEM_TICK_CYCLES_PER_US;

uint32_t zb_radio_port_clock_time_us(void)
{
	return sys_read32(TLSR8258_SYSTEM_TICK_REG);
}

bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us)
{
	return (uint32_t)(zb_radio_port_clock_time_us() - ref_us) >
	       (span_us * TLSR8258_SYSTEM_TICK_CYCLES_PER_US);
}

uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us)
{
	return delta_us / TLSR8258_SYSTEM_TICK_CYCLES_PER_US;
}

void zb_radio_port_register_rx_sink(zb_radio_port_rx_sink_t sink)
{
	zb_radio_l2_register_rx_sink(sink);
	tlsr8258_radio_register_rx_notify((sink != NULL) ? zb_platform_wake : NULL);
}

/* Overrides the weak hook the Zigbee thread calls on every pass. */
void zb_platform_radio_rx_poll(void)
{
	tlsr8258_radio_rx_poll();
}

bool zb_radio_port_ack_frame_pending_get(uint8_t seq, bool *pending)
{
	return tlsr8258_radio_ack_frame_pending_get(seq, pending);
}
