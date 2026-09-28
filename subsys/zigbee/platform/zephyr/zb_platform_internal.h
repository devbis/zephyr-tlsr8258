/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_PLATFORM_INTERNAL_H_
#define ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_PLATFORM_INTERNAL_H_

#include <stdbool.h>

/* Load the ZDO configuration attributes network discovery needs. */
void zb_platform_zdo_attr_init(void);

/*
 * True while a stack poll callback (ev_on_poll()) is enabled; the Zigbee
 * thread then keeps running ev_poll_process() instead of sleeping.
 */
bool ev_poll_any_enabled(void);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_PLATFORM_INTERNAL_H_ */
