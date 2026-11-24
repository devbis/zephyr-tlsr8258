/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/drivers/timer/system_timer_lpm.h>
#include <zephyr/irq.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "soc.h"

/*
 * Power management on top of phy62x2_suspend(): the state suspend-to-ram of the devicetree sleeps with
 * the RAM retained, and the RTC, which keeps counting, wakes the chip and keeps the time that the
 * kernel clock loses. The SysTick stops with the core, so CONFIG_SYSTEM_TIMER_RESET_BY_LPM is used and
 * the kernel restarts it. The hooks of the system timer arm the wake time and tell the elapsed time.
 */
#define AON_RTC_CNT 0x4000f028U

#define RTC_HZ 32768ULL
#define RTC_BITS 24U
/* Half of the range, so that a wrapped counter is not taken for a short sleep */
#define RTC_MAX_TICKS (BIT(RTC_BITS - 1U) - 1U)
/* The comparator must be ahead of the counter by the time that the chip needs to arm it */
#define RTC_MIN_TICKS 8U

/* The watchdog counts while the chip sleeps, which is not known; keep the sleep well short of it */
#if defined(CONFIG_PHY62X2_WATCHDOG)
#define SLEEP_LIMIT_US ((uint64_t)CONFIG_PHY62X2_WATCHDOG_TIMEOUT_MS * 500ULL)
#else
#define SLEEP_LIMIT_US UINT64_MAX
#endif

static uint32_t rtc_start;
static uint32_t sleep_ticks;

static uint32_t rtc_read(void)
{
	uint32_t first;
	uint32_t second;

	do {
		first = sys_read32(AON_RTC_CNT);
		second = sys_read32(AON_RTC_CNT);
	} while (first != second);

	return second & BIT_MASK(RTC_BITS);
}

#if defined(CONFIG_SYSTEM_TIMER_LPM_COMPANION_HOOKS)
void z_sys_clock_lpm_enter(uint64_t max_lpm_time_us)
{
	uint64_t ticks;

	if (max_lpm_time_us > SLEEP_LIMIT_US) {
		max_lpm_time_us = SLEEP_LIMIT_US;
	}
	ticks = (max_lpm_time_us * RTC_HZ) / 1000000ULL;

	sleep_ticks = (uint32_t)CLAMP(ticks, RTC_MIN_TICKS, RTC_MAX_TICKS);
	rtc_start = rtc_read();
}

uint64_t z_sys_clock_lpm_exit(void)
{
	const uint32_t elapsed = (rtc_read() - rtc_start) & BIT_MASK(RTC_BITS);

	return ((uint64_t)elapsed * 1000000ULL) / RTC_HZ;
}
#endif /* CONFIG_SYSTEM_TIMER_LPM_COMPANION_HOOKS */

#if defined(CONFIG_SERIAL)
/* The driver of the UART sets its registers again on resume and leaves the FIFO off, as at the start */
static void phy62x2_pm_state_exit(enum pm_state state)
{
	ARG_UNUSED(state);
	(void)phy62x2_uart_fifo();
}

static struct pm_notifier phy62x2_pm_notifier = {
	.state_exit = phy62x2_pm_state_exit,
};

static int phy62x2_pm_init(void)
{
	pm_notifier_register(&phy62x2_pm_notifier);

	return 0;
}

SYS_INIT(phy62x2_pm_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
#endif /* CONFIG_SERIAL */

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	if (state != PM_STATE_SUSPEND_TO_RAM) {
		return;
	}

	/* The interrupts are locked; phy62x2_suspend() returns with them locked */
	(void)phy62x2_suspend(sleep_ticks);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	/* pm_state_set() was entered with the interrupts locked and this unlocks them */
	irq_unlock(0);
}
