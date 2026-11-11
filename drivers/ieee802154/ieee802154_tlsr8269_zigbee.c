/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Receive hooks of the Zigbee radio port for a TLSR8269 without an RX thread.
 */

#include <zephyr/drivers/ieee802154/ieee802154_tlsr8269.h>
#include <zephyr/zigbee/zb_radio_port.h>

void zb_radio_port_rx_notify_set(void (*notify)(void))
{
	tlsr8269_rx_set_notify(notify);
}

void zb_radio_port_rx_service(void)
{
	tlsr8269_rx_service();
}
