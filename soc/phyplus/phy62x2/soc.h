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

#endif
