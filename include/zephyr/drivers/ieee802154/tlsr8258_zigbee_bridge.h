/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_

#include <stdint.h>

typedef void (*tlsr8258_zigbee_rx_cb_t)(const uint8_t *rx_dma, uint8_t rx_len, int8_t rssi_dbm);

void tlsr8258_zigbee_register_rx_cb(tlsr8258_zigbee_rx_cb_t cb);

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_ZIGBEE_BRIDGE_H_ */
