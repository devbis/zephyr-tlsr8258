/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_
#define ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/net/ieee802154_radio.h>

struct zb_radio_rx_frame_view {
	const uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

/* Backward-compatible alias for the original TLSR8258-specific name. */
#define tlsr8258_rx_frame_view zb_radio_rx_frame_view

typedef int (*zb_radio_port_rx_sink_t)(const struct zb_radio_rx_frame_view *frame);

enum zb_radio_port_trx_state {
	ZB_RADIO_PORT_TRX_OFF = 0,
	ZB_RADIO_PORT_TRX_RX,
	ZB_RADIO_PORT_TRX_TX,
	ZB_RADIO_PORT_TRX_ED,
	ZB_RADIO_PORT_TRX_AUTO,
};

int zb_radio_port_radio_get(const struct device **dev,
			    const struct ieee802154_radio_api **api);
/**
 * @brief Get the IEEE address used by this radio port.
 *
 * Each radio port implementation must provide this function.
 *
 * @param ieee_addr Buffer that receives the 64-bit IEEE address.
 *
 * @retval 0 Address copied successfully.
 * @retval -EINVAL The output buffer is invalid or the address length is unsupported.
 * @retval -ENODEV The radio interface or its address is not ready.
 */
int zb_radio_port_get_ieee_addr(uint8_t ieee_addr[8]);
int zb_radio_port_set_channel(uint8_t channel);
int zb_radio_port_set_trx_state(enum zb_radio_port_trx_state state,
				uint8_t channel);
uint32_t zb_radio_port_clock_time_us(void);
bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us);
uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us);
void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr);
void zb_radio_port_idle_rx_guard(void);
void zb_radio_port_watchdog_disable(void);
void zb_radio_port_watchdog_init(void);
void zb_radio_port_watchdog_feed(void);
void zb_radio_port_register_rx_sink(zb_radio_port_rx_sink_t sink);
void zb_radio_l2_register_rx_sink(zb_radio_port_rx_sink_t sink);
void zb_radio_l2_rx_poll(void);
/**
 * @brief Number of receive frames the custom L2 could not hand to the stack.
 *
 * @return Cumulative drop count since boot.
 */
uint32_t zb_radio_l2_rx_drop_count(void);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_ZB_RADIO_PORT_H_ */
