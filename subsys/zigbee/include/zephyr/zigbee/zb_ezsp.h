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
