/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_PHY62X2_SOC_H_
#define ZEPHYR_SOC_ARM_PHY62X2_SOC_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/toolchain.h>
#include <cmsis_core_m_defaults.h>

/**
 * @brief Arm the pads that wake the chip from the power off or the deep sleep
 *
 * The pads are the gpios of the enabled phyplus,phy62x2-poweroff-wakeup nodes.
 *
 * @param gpio_interrupts Also arm the pads that have an interrupt enabled in the GPIO controller,
 *                        on the edge of the interrupt
 */
void phy62x2_wakeup_arm(bool gpio_interrupts);

/**
 * @brief Enter the deep sleep
 *
 * The chip sleeps until the timeout, if there is one, or a wake pad changes its level, and then
 * starts from the boot ROM as after a reset: the registers and the RAM that is not retained are
 * lost. The RAM of the enabled zephyr,retained-ram nodes is kept, for the retained_mem driver.
 * The wake pads are those of phy62x2_wakeup_arm() with the pads of the GPIO interrupts. The
 * caller flushes the console before. Needs CONFIG_PHY62X2_DEEP_SLEEP.
 *
 * @param timeout_ms Time to wake up after, in ms, or 0 to wake on the pads only
 */
FUNC_NORETURN void phy62x2_deep_sleep(uint32_t timeout_ms);

#endif
