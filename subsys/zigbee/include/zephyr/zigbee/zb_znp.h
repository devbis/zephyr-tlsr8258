/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Z-Stack ZNP serial interface for the coordinator role. It answers the
 * subset of the Z-Stack 3.0.x MT commands that Zigbee2MQTT (adapter: zstack)
 * and zigpy-znp (ZHA) use.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

/**
 * Attach the ZNP interface to a UART device.
 *
 * @retval 0 success.
 * @retval -ENODEV @p uart_dev is NULL or not ready.
 */
int zb_znp_init(const struct device *uart_dev);

/**
 * Drain and process the bytes waiting on the ZNP UART. Non-blocking; called
 * from the Zigbee thread's own loop, never from an ISR.
 */
void zb_znp_poll(void);

/**
 * Report a BDB commissioning result to the host, for
 * zb_platform_app_bdb_commissioning_status().
 */
void zb_znp_bdb_commissioning_status(uint8_t status, bool joined_network);

/**
 * Whether the host owns commissioning. While true the application must not
 * start network formation on its own.
 */
bool zb_znp_host_controls_commissioning(void);
