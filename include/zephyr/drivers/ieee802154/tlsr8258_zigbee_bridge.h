/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_

#include <zephyr/zigbee/zb_radio_port.h>

typedef zb_radio_port_rx_sink_t tlsr8258_zigbee_rx_sink_t;

void tlsr8258_zigbee_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink);
void tlsr8258_zigbee_update_filters(uint16_t pan_id, uint16_t short_addr,
				    const uint8_t *ieee_addr);

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_ */
