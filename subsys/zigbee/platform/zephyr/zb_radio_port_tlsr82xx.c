/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The MAC clock of the TLSR8258 and TLSR8269 radio ports. The kernel tick is
 * 100 Hz, too coarse for the MAC deadlines, so the MAC counts the raw system
 * tick of the SoC. Everything else comes from zb_radio_port_generic.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/sys_io.h>
#include <zephyr/zigbee/zb_radio_port.h>

#define TLSR82XX_SYSTEM_TICK_REG 0x00800740u
#define TLSR82XX_SYSTEM_TICK_CYCLES_PER_US \
	(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000U)

/* MAC deadlines are in clock_time() units, the raw system tick. */
uint32_t sysTimerPerUs = TLSR82XX_SYSTEM_TICK_CYCLES_PER_US;

uint32_t zb_radio_port_clock_time_us(void)
{
	return sys_read32(TLSR82XX_SYSTEM_TICK_REG);
}

bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us)
{
	return (uint32_t)(zb_radio_port_clock_time_us() - ref_us) >
	       (span_us * TLSR82XX_SYSTEM_TICK_CYCLES_PER_US);
}

uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us)
{
	return delta_us / TLSR82XX_SYSTEM_TICK_CYCLES_PER_US;
}
