/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drv_radio_map.h"

static int failures;

#define EXPECT_EQ(actual, expected) do { \
	long long _actual = (long long)(actual); \
	long long _expected = (long long)(expected); \
	if (_actual != _expected) { \
		printf("FAIL %s:%d: %s=%lld expected %lld\n", __FILE__, __LINE__, \
		       #actual, _actual, _expected); \
		failures++; \
	} \
} while (0)

#define EXPECT_PTR_EQ(actual, expected) do { \
	const void *_actual = (const void *)(actual); \
	const void *_expected = (const void *)(expected); \
	if (_actual != _expected) { \
		printf("FAIL %s:%d: %s=%p expected %p\n", __FILE__, __LINE__, \
		       #actual, _actual, _expected); \
		failures++; \
	} \
} while (0)

static void test_logical_channel_mapping(void)
{
	EXPECT_EQ(zb_radio_logical_from_phy_offset(0), 11);
	EXPECT_EQ(zb_radio_logical_from_phy_offset(4), 11);
	EXPECT_EQ(zb_radio_logical_from_phy_offset(5), 11);
	EXPECT_EQ(zb_radio_logical_from_phy_offset(10), 12);
	EXPECT_EQ(zb_radio_logical_from_phy_offset(80), 26);
	EXPECT_EQ(zb_radio_logical_from_phy_offset(81), 26);
}

static void test_extract_psdu_rejects_invalid_input(void)
{
	const uint8_t dma_ok[7] = {0, 0, 0, 0, 2, 0, 0};
	const uint8_t dma_short_len[6] = {0};
	const uint8_t dma_bad_size[7] = {0, 0, 0, 0, 1, 0, 0};
	const uint8_t dma_oversized_len[7] = {0, 0, 0, 0, 6, 0, 0};
	const uint8_t dma_fcs_truncated[8] = {0, 0, 0, 0, 5, 0, 0, 0};
	const uint8_t *psdu = NULL;
	uint8_t psdu_len = 0;

	EXPECT_EQ(zb_radio_extract_psdu(NULL, sizeof(dma_ok), &psdu, &psdu_len), -22);
	EXPECT_EQ(zb_radio_extract_psdu(dma_ok, sizeof(dma_ok), NULL, &psdu_len), -22);
	EXPECT_EQ(zb_radio_extract_psdu(dma_ok, sizeof(dma_ok), &psdu, NULL), -22);
	EXPECT_EQ(zb_radio_extract_psdu(dma_short_len, sizeof(dma_short_len), &psdu, &psdu_len),
		  -22);
	EXPECT_EQ(zb_radio_extract_psdu(dma_bad_size, sizeof(dma_bad_size), &psdu, &psdu_len),
		  -22);
	EXPECT_EQ(zb_radio_extract_psdu(dma_oversized_len, sizeof(dma_oversized_len),
					&psdu, &psdu_len), -22);
	EXPECT_EQ(zb_radio_extract_psdu(dma_fcs_truncated, sizeof(dma_fcs_truncated),
					&psdu, &psdu_len), -22);
}

static void test_extract_psdu_returns_payload(void)
{
	const uint8_t dma[10] = {0, 0, 0, 0, 5, 0x11, 0x22, 0x33, 0x44, 0x55};
	const uint8_t *psdu = NULL;
	uint8_t psdu_len = 0;

	EXPECT_EQ(zb_radio_extract_psdu(dma, sizeof(dma), &psdu, &psdu_len), 0);
	EXPECT_EQ(psdu_len, 3);
	EXPECT_PTR_EQ(psdu, &dma[5]);
	EXPECT_EQ(memcmp(psdu, &dma[5], psdu_len), 0);
}

static void test_tx_power_level_mapping(void)
{
	EXPECT_EQ(zb_radio_tx_dbm_from_level(23), 0);
	EXPECT_EQ(zb_radio_tx_dbm_from_level(0), 0);
	EXPECT_EQ(zb_radio_tx_dbm_from_level(7), 7);
	EXPECT_EQ(zb_radio_tx_dbm_from_level(11), 11);
	EXPECT_EQ(zb_radio_tx_dbm_from_level(12), 0);
}

int main(void)
{
	test_logical_channel_mapping();
	test_extract_psdu_rejects_invalid_input();
	test_extract_psdu_returns_payload();
	test_tx_power_level_mapping();

	if (failures != 0) {
		printf("host_radio_map: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_radio_map: PASS\n");
	return 0;
}
