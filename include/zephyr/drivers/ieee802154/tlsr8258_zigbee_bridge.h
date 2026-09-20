/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_

#include <zephyr/zigbee/zb_radio_port.h>

typedef zb_radio_port_rx_sink_t tlsr8258_zigbee_rx_sink_t;

void tlsr8258_zigbee_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink);
void tlsr8258_zigbee_update_filters(uint16_t pan_id, uint16_t short_addr,
				    const uint8_t *ieee_addr);
void tlsr8258_zigbee_idle_rx_guard(void);

/*
 * Hardware watchdog (timer2-based), fed once per zb_thread main-loop pass.
 * Converts any thread hang -- a plain infinite loop or a stuck hardware bus
 * access, neither of which raises a CPU fault -- into a forced reboot
 * instead of a permanent silent death. See ieee802154_tlsr8258.c for the
 * register-level detail and TLSR_WATCHDOG_PERIOD_MS.
 */
void tlsr8258_watchdog_disable(void);
void tlsr8258_watchdog_init(void);
void tlsr8258_watchdog_feed(void);

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_ */
