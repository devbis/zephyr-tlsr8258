/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * @file
 * @brief EZSP interface of the Zigbee coordinator
 *
 * Lets a host drive the coordinator with the serial protocol of Silicon Labs
 * EmberZNet (EZSP over ASH), limited to the commands Zigbee2MQTT (adapter:
 * ember), ZHA (bellows) and homed-zigbee use.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

/**
 * Attach the EZSP interface to a UART device and announce the start to the host.
 *
 * @retval 0 success.
 * @retval -ENODEV @p uart_dev is NULL or not ready.
 */
int zb_ezsp_init(const struct device *uart_dev);

/**
 * Process the bytes waiting on the EZSP UART and send what is due. Non-blocking; called from the
 * Zigbee thread's own loop, never from an ISR.
 */
void zb_ezsp_poll(void);

/**
 * Report a BDB commissioning result to the host, for
 * zb_platform_app_bdb_commissioning_status(). A network formation the host asked for ends in a
 * stack status callback.
 */
void zb_ezsp_bdb_commissioning_status(uint8_t status, bool joined_network);
