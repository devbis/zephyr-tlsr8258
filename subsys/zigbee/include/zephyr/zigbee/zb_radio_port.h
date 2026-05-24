/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_
#define ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/net/ieee802154_radio.h>

struct zb_radio_port_rx_frame {
	const uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

typedef int (*zb_radio_port_rx_sink_t)(const struct zb_radio_port_rx_frame *frame);

enum zb_radio_port_trx_state {
	ZB_RADIO_PORT_TRX_OFF = 0,
	ZB_RADIO_PORT_TRX_RX,
	ZB_RADIO_PORT_TRX_TX,
	ZB_RADIO_PORT_TRX_ED,
	ZB_RADIO_PORT_TRX_AUTO,
};

int zb_radio_port_radio_get(const struct device **dev,
			    const struct ieee802154_radio_api **api);
int zb_radio_port_set_channel(uint8_t channel);
int zb_radio_port_set_trx_state(enum zb_radio_port_trx_state state,
				uint8_t channel);
uint32_t zb_radio_port_clock_time_us(void);
bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us);
uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us);
void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr);
void zb_radio_port_register_rx_sink(zb_radio_port_rx_sink_t sink);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_ */
