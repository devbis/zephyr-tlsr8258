/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_
#define ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_

#include <stdint.h>

typedef void (*zb_radio_port_rx_cb_t)(const uint8_t *rx_dma, uint8_t rx_len,
				      int8_t rssi_dbm);

void zb_radio_port_register_rx_cb(zb_radio_port_rx_cb_t cb);
void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_ */
