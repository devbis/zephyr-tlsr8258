/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h>
#include <zephyr/zigbee/zb_radio_port.h>

void zb_radio_port_register_rx_cb(zb_radio_port_rx_cb_t cb)
{
	tlsr8258_zigbee_register_rx_cb((tlsr8258_zigbee_rx_cb_t)cb);
}

void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr)
{
	tlsr8258_zigbee_update_filters(pan_id, short_addr, ieee_addr);
}
