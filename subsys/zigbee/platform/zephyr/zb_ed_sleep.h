/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_ED_SLEEP_H_
#define ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_ED_SLEEP_H_

/**
 * @brief Enter deep sleep when an SED has no pending Zigbee work.
 */
void zb_ed_sleep_maybe(void);

#endif
