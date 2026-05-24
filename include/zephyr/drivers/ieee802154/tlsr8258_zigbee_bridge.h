/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_

#include <stdint.h>

struct tlsr8258_rx_frame_view {
	const uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

typedef int (*tlsr8258_zigbee_rx_sink_t)(const struct tlsr8258_rx_frame_view *frame);

void tlsr8258_zigbee_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink);
void tlsr8258_zigbee_update_filters(uint16_t pan_id, uint16_t short_addr,
				    const uint8_t *ieee_addr);

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_ */
