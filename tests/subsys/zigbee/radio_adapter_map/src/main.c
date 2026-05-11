/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/ztest.h>

#include "drv_radio_map.h"

ZTEST(radio_adapter_map, test_logical_channel_mapping)
{
	zassert_equal(zb_radio_logical_from_phy_offset(0), 11);
	zassert_equal(zb_radio_logical_from_phy_offset(4), 11);
	zassert_equal(zb_radio_logical_from_phy_offset(5), 11);
	zassert_equal(zb_radio_logical_from_phy_offset(10), 12);
	zassert_equal(zb_radio_logical_from_phy_offset(80), 26);
	zassert_equal(zb_radio_logical_from_phy_offset(81), 26);
}

ZTEST(radio_adapter_map, test_extract_psdu_rejects_invalid_input)
{
	const uint8_t dma_ok[7] = {0, 0, 0, 0, 2, 0, 0};
	const uint8_t dma_short_len[6] = {0};
	const uint8_t dma_bad_size[7] = {0, 0, 0, 0, 1, 0, 0};
	const uint8_t dma_oversized_len[7] = {0, 0, 0, 0, 6, 0, 0};
	const uint8_t *psdu = NULL;
	uint8_t psdu_len = 0;

	zassert_equal(zb_radio_extract_psdu(NULL, sizeof(dma_ok), &psdu, &psdu_len), -EINVAL);
	zassert_equal(zb_radio_extract_psdu(dma_ok, sizeof(dma_ok), NULL, &psdu_len), -EINVAL);
	zassert_equal(zb_radio_extract_psdu(dma_ok, sizeof(dma_ok), &psdu, NULL), -EINVAL);
	zassert_equal(zb_radio_extract_psdu(dma_short_len, sizeof(dma_short_len), &psdu, &psdu_len),
		      -EINVAL);
	zassert_equal(zb_radio_extract_psdu(dma_bad_size, sizeof(dma_bad_size), &psdu, &psdu_len),
		      -EINVAL);
	zassert_equal(
		zb_radio_extract_psdu(dma_oversized_len, sizeof(dma_oversized_len), &psdu, &psdu_len),
		-EINVAL);
}

ZTEST(radio_adapter_map, test_extract_psdu_returns_payload)
{
	const uint8_t dma[10] = {0, 0, 0, 0, 5, 0x11, 0x22, 0x33, 0x44, 0x55};
	const uint8_t *psdu = NULL;
	uint8_t psdu_len = 0;

	zassert_equal(zb_radio_extract_psdu(dma, sizeof(dma), &psdu, &psdu_len), 0);
	zassert_equal(psdu_len, 3);
	zassert_equal_ptr(psdu, &dma[5]);
	zassert_mem_equal(psdu, &dma[5], psdu_len);
}

ZTEST_SUITE(radio_adapter_map, NULL, NULL, NULL, NULL, NULL);
