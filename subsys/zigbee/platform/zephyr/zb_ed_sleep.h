/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_ED_SLEEP_H_
#define ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_ED_SLEEP_H_

#include <stdbool.h>

#include <stdint.h>

/**
 * @brief Let the SoC enter deep sleep while an SED has no pending Zigbee work.
 *
 * Releases or takes the PM_STATE_SOFT_OFF policy lock according to the stack
 * state. Only built with CONFIG_ZIGBEE_SLEEPY_DEEP.
 */
void zb_ed_sleep_maybe(void);

/**
 * @brief Report whether this boot followed a deep sleep of the end device.
 *
 * @retval true A valid retained record from the last deep sleep is present.
 * @retval false This is a cold boot or a reset unrelated to deep sleep.
 */
bool zb_ed_sleep_woke_from_deep(void);

/**
 * @brief Read the outgoing frame counter saved before the last deep sleep.
 *
 * @param frame_counter Destination for the counter.
 *
 * @retval 0 Counter returned.
 * @retval -EINVAL frame_counter is NULL.
 * @retval -ENOENT No valid retained record is present.
 */
int zb_ed_sleep_retained_frame_counter(uint32_t *frame_counter);

/**
 * @brief Drop the retained record once the wake has been handled.
 */
void zb_ed_sleep_wake_clear(void);

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
