/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_DRV_RADIO_MAP_H_
#define ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_DRV_RADIO_MAP_H_

#include <stdint.h>

uint8_t zb_radio_logical_from_phy_offset(uint8_t phy);
int zb_radio_extract_psdu(const uint8_t *dma, uint8_t dma_len,
			  const uint8_t **psdu, uint8_t *psdu_len);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_DRV_RADIO_MAP_H_ */
