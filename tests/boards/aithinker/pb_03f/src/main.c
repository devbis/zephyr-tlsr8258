/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#define GPIO_NODE DT_NODELABEL(gpio0)
#define VALID_PINS GPIO_PORT_PIN_MASK_FROM_DT_NODE(GPIO_NODE)
#define PRESENT_PINS (BIT(0) | BIT(4) | BIT(7) | BIT(12) | BIT(22))

BUILD_ASSERT((VALID_PINS & (BIT(1) | BIT(5) | BIT(6) | BIT(10) | BIT(11) |
			    BIT(16) | BIT(17) | BIT(18))) == 0U);
BUILD_ASSERT((VALID_PINS & PRESENT_PINS) == PRESENT_PINS);
BUILD_ASSERT(PARTITION_OFFSET(storage_partition) == 0x3d000U);
BUILD_ASSERT(PARTITION_SIZE(storage_partition) == 0x3000U);
BUILD_ASSERT(DT_PROP(DT_NODELABEL(timer5), clock_frequency) == 4000000U);
BUILD_ASSERT(DT_PROP(DT_NODELABEL(timer6), clock_frequency) == 4000000U);

ZTEST(pb_03f, test_devices_ready)
{
	zassert_true(device_is_ready(DEVICE_DT_GET(GPIO_NODE)));
	zassert_true(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(timer5))));
	zassert_true(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(timer6))));
	zassert_true(device_is_ready(PARTITION_DEVICE(storage_partition)));
}

ZTEST_SUITE(pb_03f, NULL, NULL, NULL, NULL, NULL);
