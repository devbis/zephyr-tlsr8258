/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/*
 * Compile-time channel selection derived from CONFIG_ZIGBEE_CHANNEL_MASK.
 *
 * ZB_CHANNEL_SCAN_MASK  channels scanned when joining (router, end device)
 *                       or considered when forming (coordinator)
 * ZB_CHANNEL_INITIAL    single channel the radio is started on before any
 *                       scan result exists
 */

#define ZB_CHANNEL_ALL_MASK 0x07FFF800UL

#if ((CONFIG_ZIGBEE_CHANNEL_MASK & ZB_CHANNEL_ALL_MASK) == 0)
#error "CONFIG_ZIGBEE_CHANNEL_MASK selects no channel in 11..26"
#endif
#define ZB_CHANNEL_SCAN_MASK ((unsigned long)CONFIG_ZIGBEE_CHANNEL_MASK & ZB_CHANNEL_ALL_MASK)
#define ZB_CHANNEL_INITIAL   __builtin_ctzl(ZB_CHANNEL_SCAN_MASK)
