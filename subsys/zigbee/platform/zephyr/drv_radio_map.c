/* SPDX-License-Identifier: Apache-2.0 */

#include "drv_radio_map.h"

#include <errno.h>

uint8_t zb_radio_logical_from_phy_offset(uint8_t phy)
{
	if (phy < 5) {
		return 11;
	}

	if (phy > 80) {
		return 26;
	}

	return (uint8_t)(10 + (phy / 5));
}

int zb_radio_extract_psdu(const uint8_t *dma, uint8_t dma_len,
			  const uint8_t **psdu, uint8_t *psdu_len)
{
	uint8_t payload_len;
	uint8_t available_len;

	if (!dma || !psdu || !psdu_len || dma_len < 7) {
		return -EINVAL;
	}

	payload_len = dma[4];
	if (payload_len < 2) {
		return -EINVAL;
	}

	available_len = (uint8_t)(dma_len - 5);
	if (payload_len > available_len) {
		return -EINVAL;
	}

	*psdu_len = (uint8_t)(payload_len - 2);
	*psdu = &dma[5];
	return 0;
}

int16_t zb_radio_tx_dbm_from_level(uint8_t level)
{
	if (level == 23U) {
		return 0;
	}

	if (level <= 11U) {
		return (int16_t)level;
	}

	return 0;
}
