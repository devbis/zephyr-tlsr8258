/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_ED_SLEEP_H_
#define ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_ED_SLEEP_H_

#include <stdbool.h>

/**
 * @brief Enter deep sleep when an SED has no pending Zigbee work.
 */
void zb_ed_sleep_maybe(void);

/**
 * @brief Open or extend the fast poll window after a frame from the parent.
 *
 * Safe to call from the receive path: it only records a deadline.
 */
void zb_ed_poll_note_activity(void);

/**
 * @brief Report whether the fast poll window is open or still applied.
 */
bool zb_ed_poll_fast_active(void);

/**
 * @brief Switch the parent poll interval between fast and slow.
 *
 * Called from the Zigbee thread on every pass.
 */
void zb_ed_poll_service(void);

/**
 * @brief Make the parent poll fire as soon as possible.
 *
 * Used after a deep-sleep wake, where the poll timer was re-armed with a
 * full interval although the wake itself was the poll deadline.
 */
void zb_ed_poll_poll_now(void);

#endif
