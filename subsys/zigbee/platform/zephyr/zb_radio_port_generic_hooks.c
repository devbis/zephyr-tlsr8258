/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/ieee802154/native_sim_socket.h>
#include <zephyr/kernel.h>
#include <zephyr/zigbee/zb_radio_port.h>

/* MAC deadlines are in clock_time() units, which count microseconds here. */
uint32_t sysTimerPerUs = 1U;

uint32_t zb_radio_port_clock_time_us(void)
{
	return (uint32_t)k_ticks_to_us_floor32(k_uptime_ticks());
}

bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us)
{
	return (uint32_t)(zb_radio_port_clock_time_us() - ref_us) > span_us;
}

uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us)
{
	return delta_us;
}

#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET)
/* The socket driver has no RX thread; drain it from the Zigbee thread. */
void zb_platform_radio_rx_poll(void)
{
	ieee802154_native_sim_socket_poll();
}
#endif
