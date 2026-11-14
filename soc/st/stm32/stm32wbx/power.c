/*
 * Copyright (c) 2019 STMicroelectronics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <soc.h>
#include <zephyr/init.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>

#include <stm32_ll_utils.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_cortex.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>
#include <stm32_ll_rtc.h>
#include <stm32_backup_domain.h>
#include <timeout_q.h>
#include <clock_control/clock_stm32_ll_common.h>
#include <stm32_hsem.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(soc, CONFIG_SOC_LOG_LEVEL);

/*
 * @brief Switch the system clock on HSI
 * @param none
 * @retval none
 */
static void switch_on_hsi(void)
{
	LL_RCC_HSI_Enable();
	while (!LL_RCC_HSI_IsReady()) {
	}

	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
	LL_RCC_SetSMPSClockSource(LL_RCC_SMPS_CLKSOURCE_HSI);
	while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI) {
	}
}

static void lpm_hsem_lock(void)
{
	/* Implementation of STM32 AN5289 algorithm to enter/exit lowpower */
	z_stm32_hsem_lock(CFG_HW_RCC_SEMID, HSEM_LOCK_WAIT_FOREVER);

	if (!LL_HSEM_1StepLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID)) {
		if (LL_PWR_IsActiveFlag_C2DS()) {
			/* Release ENTRY_STOP_MODE semaphore */
			LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);

			/* The switch on HSI before entering Stop Mode is required */
			switch_on_hsi();
		}
	} else {
		/* The switch on HSI before entering Stop Mode is required */
		switch_on_hsi();
	}
}

/*
 * Standby (PM_STATE_SOFT_OFF). The system enters Standby only when both CPUs are in deep sleep
 * and the radio firmware of CPU2 never gets there, even with the radio disabled. CPU2 is held
 * until the application starts it, so the state is entered from a reset instead:
 *
 *  1. pm_state_set() stores the time to sleep and a magic value in the backup registers and
 *     resets the SoC. The notifiers have saved what must survive before.
 *  2. standby_boot() runs early, before CPU2 is started, finds the magic, arms the RTC wakeup
 *     timer and enters Standby with CPU2 still held.
 *  3. The wakeup is a reset again, and the application starts as after a power-up.
 *
 * Only the backup registers keep their content across it. MCUboot starts the application at
 * once while one of the magic values is present, without its update window or image check; the
 * application clears the value when it runs (see the wbfix module of the zigbee_shell sample),
 * so a hang after the wakeup still opens the window through the watchdog.
 */
#define STANDBY_NODE		DT_NODELABEL(standby)
#define STANDBY_LATENCY_MS	DIV_ROUND_UP(DT_PROP_OR(STANDBY_NODE, exit_latency_us, 0), 1000U)
#define STANDBY_MAGIC_SLEEP	0x53424f53U
#define STANDBY_MAGIC_WAKE	0x53424f4fU
#define STANDBY_MAGIC_REG	(RTC->BKP19R)
#define STANDBY_MS_REG		(RTC->BKP18R)

/* The RTC wakeup timer counts RTCCLK / 16 (2048 Hz on the LSE) or ck_spre (1 Hz) */
#define WUT_FAST_HZ		2048U
#define WUT_FAST_MAX_MS		(0xffffU * 1000U / WUT_FAST_HZ)
#define WUT_SLOW_MAX_S		0x1ffffU

static void standby_rtc_arm(uint32_t ms)
{
	uint32_t count;
	uint32_t clock;

	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_RTCAPB);

	if (LL_RCC_GetRTCClockSource() == LL_RCC_RTC_CLKSOURCE_NONE) {
		LL_RCC_SetRTCClockSource(LL_RCC_RTC_CLKSOURCE_LSE);
	}
	LL_RCC_EnableRTC();

	if (ms <= WUT_FAST_MAX_MS) {
		clock = LL_RTC_WAKEUPCLOCK_DIV_16;
		count = MAX(((uint64_t)ms * WUT_FAST_HZ) / 1000U, 1U);
	} else {
		uint32_t s = MIN(ms / 1000U, WUT_SLOW_MAX_S);

		clock = (s > 0xffffU) ? LL_RTC_WAKEUPCLOCK_CKSPRE_WUT : LL_RTC_WAKEUPCLOCK_CKSPRE;
		count = (s > 0xffffU) ? (s - 0x10000U) : s;
	}

	LL_RTC_DisableWriteProtection(RTC);
	LL_RTC_WAKEUP_Disable(RTC);
	while (LL_RTC_IsActiveFlag_WUTW(RTC) == 0U) {
	}
	LL_RTC_WAKEUP_SetClock(RTC, clock);
	LL_RTC_WAKEUP_SetAutoReload(RTC, count);
	LL_RTC_ClearFlag_WUT(RTC);
	LL_RTC_EnableIT_WUT(RTC);
	LL_RTC_WAKEUP_Enable(RTC);
	LL_RTC_EnableWriteProtection(RTC);
}


static void standby_enter(void)
{
	uint32_t ticks = z_get_next_timeout_expiry();
	uint64_t ms = k_ticks_to_ms_floor64((ticks > (uint32_t)INT32_MAX) ? INT32_MAX : ticks);

	ms = (ms > STANDBY_LATENCY_MS) ? (ms - STANDBY_LATENCY_MS) : 1U;

	stm32_backup_domain_enable_access();
	STANDBY_MS_REG = (uint32_t)MIN(ms, (uint64_t)UINT32_MAX);
	STANDBY_MAGIC_REG = STANDBY_MAGIC_SLEEP;
	stm32_backup_domain_disable_access();

	NVIC_SystemReset();
}

static enum stm32wb_standby_wake standby_wake;

enum stm32wb_standby_wake stm32wb_standby_wake_get(void)
{
	return standby_wake;
}

/*
 * Runs after the clocks are up and before the radio driver starts CPU2.
 */
static int standby_boot(void)
{
	/*
	 * Keep Standby locked until the application has its own gating in place; the first idle
	 * period comes before it and would otherwise restart the SoC again and again.
	 * zb_ed_sleep_init() releases this lock.
	 */
	if (IS_ENABLED(CONFIG_ZIGBEE_SLEEPY_DEEP)) {
		pm_policy_state_lock_get(PM_STATE_SOFT_OFF, PM_ALL_SUBSTATES);
	}

	if (STANDBY_MAGIC_REG == STANDBY_MAGIC_SLEEP) {
		stm32_backup_domain_enable_access();
		STANDBY_MAGIC_REG = STANDBY_MAGIC_WAKE;
		standby_rtc_arm(STANDBY_MS_REG);

		(void)irq_lock();
		LL_PWR_ClearFlag_WU();
		LL_PWR_ClearFlag_C1STOP_C1STB();
		LL_PWR_EnableInternWU();
		LL_PWR_SetPowerMode(LL_PWR_MODE_STANDBY);
		LL_LPM_EnableDeepSleep();
		__WFI();

		/* The system did not enter Standby: start as after a reset */
		STANDBY_MAGIC_REG = 0U;
		NVIC_SystemReset();
	}

	if (LL_PWR_IsActiveFlag_C1SB() != 0U) {
		/*
		 * The timer flag is cleared before every entry and the RTC survives a pin reset, so
		 * a Standby exit without it was not the timer.
		 */
		LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_RTCAPB);
		if (LL_RTC_IsActiveFlag_WUT(RTC) != 0U) {
			standby_wake = STM32WB_STANDBY_WAKE_STANDBY;
		} else {
			standby_wake = STM32WB_STANDBY_WAKE_RESET_PIN;
		}
	}
	if (STANDBY_MAGIC_REG != 0U) {
		stm32_backup_domain_enable_access();
		STANDBY_MAGIC_REG = 0U;
		stm32_backup_domain_disable_access();
	}
	LL_PWR_ClearFlag_C1STOP_C1STB();

	return 0;
}
SYS_INIT(standby_boot, POST_KERNEL, 0);

/* Invoke Low Power/System Off specific Tasks */
void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	if (state == PM_STATE_SOFT_OFF) {
		standby_enter();
	} else if (state == PM_STATE_SUSPEND_TO_IDLE) {

		lpm_hsem_lock();

		/* ensure HSI is the wake-up system clock */
		LL_RCC_SetClkAfterWakeFromStop(LL_RCC_STOP_WAKEUPCLOCK_HSI);

		switch (substate_id) {
		case 1:
			/* enter STOP0 mode */
			LL_PWR_SetPowerMode(LL_PWR_MODE_STOP0);
			break;
		case 2:
			/* enter STOP1 mode */
			LL_PWR_SetPowerMode(LL_PWR_MODE_STOP1);
			break;
		case 3:
			/* enter STOP2 mode */
			LL_PWR_SetPowerMode(LL_PWR_MODE_STOP2);
			break;
		default:
			/* Release RCC semaphore */
			z_stm32_hsem_unlock(CFG_HW_RCC_SEMID);
			LOG_DBG("Unsupported power substate-id %u", substate_id);
			return;
		}

		if (IS_ENABLED(STM32_HSI48_ENABLED)) {
			/*
			 * Release CLK48 semaphore to make sure M0 core can enable/disable
			 * it as needed (shared between RNG and USB peripheral, M0 uses RNG
			 * during BLE advertisement phase). It seems like if left locked M0
			 * can enable the clock if needed but is not able (allowed) to stop
			 * it, with increased power consumption as a result.
			 */
			z_stm32_hsem_unlock(CFG_HW_CLK48_CONFIG_SEMID);
		}

		/* Release RCC semaphore */
		z_stm32_hsem_unlock(CFG_HW_RCC_SEMID);

		LL_LPM_EnableDeepSleep();

		/* enter SLEEP mode : WFE or WFI */
		k_cpu_idle();
	} else {
		LOG_DBG("Unsupported power state %u", state);
		return;
	}
}

/* Handle SOC specific activity after Low Power Mode Exit */
void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	/* Implementation of STM32 AN5289 algorithm to enter/exit lowpower */
	/* Release ENTRY_STOP_MODE semaphore */
	LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);
	z_stm32_hsem_lock(CFG_HW_RCC_SEMID, HSEM_LOCK_WAIT_FOREVER);

	if (state != PM_STATE_SUSPEND_TO_IDLE) {
		LOG_DBG("Unsupported power state %u", state);
	} else {
		switch (substate_id) {
		case 1:	/* STOP0 */
			__fallthrough;
		case 2:	/* STOP1 */
			__fallthrough;
		case 3:	/* STOP2 */
			LL_LPM_DisableSleepOnExit();
			LL_LPM_EnableSleep();
			break;
		default:
			LOG_DBG("Unsupported power substate-id %u",
				substate_id);
			break;
		}
		/* need to restore the clock */
		stm32_clock_control_init(NULL);
	}

	/* Release RCC semaphore */
	z_stm32_hsem_unlock(CFG_HW_RCC_SEMID);

	/*
	 * System is now in active mode.
	 * Reenable interrupts which were disabled
	 * when OS started idling code.
	 */
	irq_unlock(0);
}

/* Initialize STM32 Power */
void stm32_pm_init(void)
{
}
