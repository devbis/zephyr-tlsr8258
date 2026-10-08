/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8269_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8269_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Received frame in the layout the Zigbee L2 uses: a 5-byte header whose byte
 * 4 is the PHY length (PSDU plus FCS), followed by the PSDU and the FCS.
 */
#define TLSR8269_RX_FRAME_PSDU_OFFSET 5U

struct tlsr8269_rx_frame_view {
	/** Frame in the layout above. */
	const uint8_t *dma;
	/** Number of valid bytes in @p dma. */
	uint8_t len;
	/** Received signal strength in dBm. */
	int8_t rssi_dbm;
};

typedef int (*tlsr8269_rx_sink_t)(const struct tlsr8269_rx_frame_view *frame);
typedef void (*tlsr8269_rx_notify_t)(void);

void tlsr8269_radio_register_rx_sink(tlsr8269_rx_sink_t sink);
void tlsr8269_radio_register_rx_notify(tlsr8269_rx_notify_t notify);
void tlsr8269_radio_rx_poll(void);
void tlsr8269_radio_update_filters(uint16_t pan_id, uint16_t short_addr,
				   const uint8_t *ieee_addr);
int tlsr8269_radio_get_ieee_addr(uint8_t ieee_addr[8]);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8269_H_ */
