/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/ztest.h>
#include "flash_phy6252_util.h"

ZTEST(phy6252_flash, test_ranges)
{
	zassert_true(phy6252_flash_range_valid(0, 1));
	zassert_true(phy6252_flash_range_valid(0x3ffff, 1));
	zassert_true(phy6252_flash_range_valid(0x40000, 0));
	zassert_false(phy6252_flash_range_valid(-1, 1));
	zassert_false(phy6252_flash_range_valid(0x40000, 1));
	zassert_false(phy6252_flash_range_valid(0x3ffff, 2));
	zassert_false(phy6252_flash_range_valid(1, SIZE_MAX));
}

ZTEST(phy6252_flash, test_page_chunks)
{
	zassert_equal(phy6252_flash_write_chunk(0, 0), 0);
	zassert_equal(phy6252_flash_write_chunk(0, 5), 5);
	zassert_equal(phy6252_flash_write_chunk(0, 256), 64);
	zassert_equal(phy6252_flash_write_chunk(250, 64), 6);
	zassert_equal(phy6252_flash_write_chunk(255, 64), 1);
	zassert_equal(phy6252_flash_write_chunk(256, 64), 64);
}

ZTEST(phy6252_flash, test_rom_addresses)
{
	zassert_equal(phy6252_flash_address(0U), PHY6252_FLASH_BASE);
	zassert_equal(phy6252_flash_address(0x3d000U), 0x1103d000U);
}

ZTEST_SUITE(phy6252_flash, NULL, NULL, NULL, NULL, NULL);
