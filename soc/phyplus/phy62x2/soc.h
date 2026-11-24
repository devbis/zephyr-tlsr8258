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

/* PHY62XX SDK 3.1.1 ROM clock entry points. */
#define ROM_CLK_INIT     ((int (*)(uint32_t))0x0000a5edU)
#define ROM_CLK_GET_PCLK ((uint32_t (*)(void))0x0000a5d1U)
#define SYS_CLK_XTAL_16M 2U
#define SYS_CLK_DLL_48M  3U
#define ROM_SPIF_CONFIG  ((int (*)(uint32_t, uint8_t, uint32_t, uint8_t, uint8_t))0x00016dc5U)
#define XFRD_FCMD_READ   0x00000003U

/* The core clock is the CPU timebase of the devicetree: the 16 MHz crystal or the 48 MHz DLL. */
#if CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC == 16000000
#define PHY62X2_SYS_CLK SYS_CLK_XTAL_16M
#elif CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC == 48000000
#define PHY62X2_SYS_CLK SYS_CLK_DLL_48M
#else
#error "The CPU timebase frequency must be 16 MHz or 48 MHz"
#endif

/** @brief Select the core clock of the devicetree through the ROM, false if the peripheral clock differs */
bool phy62x2_clock_init(void);

/** @brief Configure the read of the flash and enable the cache of the XIP, runs from the RAM */
void phy62x2_xip_cache_enable(void);

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

/**
 * @brief Suspend to RAM
 *
 * The chip sleeps with its RAM retained until the RTC reaches the time or a wake pad changes its
 * level, then the registers of the program, the clock, the cache of the flash, the clock gates, the
 * NVIC and the SysTick are restored and the function returns. The registers of the other
 * peripherals are lost in the sleep. Needs CONFIG_PHY62X2_SUSPEND.
 *
 * @param rtc_ticks Time to wake up after, in ticks of the RTC (32768 Hz), or 0 to wake on the pads only
 *
 * @retval 0 after the wakeup
 */
int phy62x2_suspend(uint32_t rtc_ticks);

#endif
