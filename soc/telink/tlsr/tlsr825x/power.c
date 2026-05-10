/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#include <zephyr/sys/util.h>

#include <zephyr/arch/tc32/irq.h>

#include "power.h"

extern void start_suspend(void);

#define TLSR8258_REG8(addr)  (*(volatile uint8_t *)(addr))
#define TLSR8258_REG16(addr) (*(volatile uint16_t *)(addr))
#define TLSR8258_REG32(addr) (*(volatile uint32_t *)(addr))

#define TLSR8258_REG_CLK_SEL             TLSR8258_REG8(0x00800066u)
#define TLSR8258_REG_MCU_WAKEUP_MASK     TLSR8258_REG32(0x00800078u)
#define TLSR8258_REG_PWDN_CTRL           TLSR8258_REG8(0x0080006fu)
#define TLSR8258_REG_PM_RET_SRAM_CTRL    TLSR8258_REG8(0x00800602u)
#define TLSR8258_REG_RET_SLOT_IDX        TLSR8258_REG8(0x0080060du)
#define TLSR8258_REG_GPIO_WAKEUP_IRQ     TLSR8258_REG8(0x008005b5u)
#define TLSR8258_REG_PM_MISC_DUMMY       TLSR8258_REG32(0x00800040u)
#define TLSR8258_REG_TMR_CTRL8           TLSR8258_REG8(0x00800620u)
#define TLSR8258_REG_TMR1_CAPT           TLSR8258_REG32(0x00800628u)
#define TLSR8258_REG_TMR1_TICK           TLSR8258_REG32(0x00800634u)
#define TLSR8258_REG_RF_IRQ_STATUS       TLSR8258_REG16(0x00800f20u)
#define TLSR8258_REG_SYSTEM_TICK         TLSR8258_REG32(0x00800740u)
#define TLSR8258_REG_SYSTEM_TICK_IRQ     TLSR8258_REG32(0x00800744u)
#define TLSR8258_REG_SYSTEM_TICK_MODE    TLSR8258_REG8(0x0080074cu)
#define TLSR8258_REG_SYSTEM_TICK_CTRL    TLSR8258_REG8(0x0080074fu)
#define TLSR8258_REG_SYSTEM_32K_TICK_RD  TLSR8258_REG16(0x00800750u)
#define TLSR8258_REG_SYSTEM_32K_TICK_CAL TLSR8258_REG32(0x00800754u)

#define TLSR8258_REG_ANA_ADDR TLSR8258_REG8(0x008000b8u)
#define TLSR8258_REG_ANA_DATA TLSR8258_REG8(0x008000b9u)
#define TLSR8258_REG_ANA_CTRL TLSR8258_REG8(0x008000bau)

#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

#define TLSR8258_FLD_GPIO_CORE_WAKEUP_EN    BIT(2)
#define TLSR8258_FLD_GPIO_CORE_INTERRUPT_EN BIT(3)

#define TLSR8258_PM_WAKEUP_PAD_BITS        BIT(4)
#define TLSR8258_PM_WAKEUP_CORE_BITS       BIT(5)
#define TLSR8258_PM_WAKEUP_TIMER_BITS      BIT(6)
#define TLSR8258_PM_WAKEUP_COMPARATOR_BITS BIT(7)

#define TLSR8258_WAKEUP_SRC_GPIO       BIT(3)
#define TLSR8258_WAKEUP_SRC_GPIO_RM    BIT(5)
#define TLSR8258_WAKEUP_SRC_TMR1       BIT(1)
#define TLSR8258_FLD_TMR1_EN           BIT(3)
#define TLSR8258_FLD_TMR1_MODE         (BIT(4) | BIT(5))
#define TLSR8258_FLD_TMR_STA_TMR1      BIT(1)
#define TLSR8258_FLD_IRQ_TMR1_EN       BIT(1)
#define TLSR8258_FLD_IRQ_ZB_RT_EN      BIT(13)
#define TLSR8258_FLD_PWDN_CTRL_SLEEP   BIT(7)

#define TLSR8258_WAKEUP_STATUS_COMPARATOR BIT(0)
#define TLSR8258_WAKEUP_STATUS_TIMER      BIT(1)
#define TLSR8258_WAKEUP_STATUS_CORE       BIT(2)
#define TLSR8258_WAKEUP_STATUS_PAD        BIT(3)
#define TLSR8258_WAKEUP_STATUS_ALL        0x0fu
#define TLSR8258_STATUS_GPIO_ERR_NO_ENTER_PM BIT(8)
#define TLSR8258_STATUS_ENTER_SUSPEND        BIT(30)

#define TLSR8258_AREG_CLK_2M_RC      0x02u
#define TLSR8258_AREG_LDO_SETTING1   0x07u
#define TLSR8258_AREG_0X04           0x04u
#define TLSR8258_AREG_0X1F           0x1fu
#define TLSR8258_AREG_0X20           0x20u
#define TLSR8258_AREG_0X26           0x26u
#define TLSR8258_AREG_0X2B           0x2bu
#define TLSR8258_AREG_0X2C           0x2cu
#define TLSR8258_AREG_0X7E           0x7eu
#define TLSR8258_AREG_WAKEUP_STATUS  0x44u
#define TLSR8258_AREG_PM_STATUS      0x7fu

#define TLSR8258_PM_RET_ENTRY_BASE   0x00840058u
#define TLSR8258_PM_RET_SLOT_NOP     0x06c006c0u

#define TLSR8258_PM_SYS_TICK_PER_US 16u
#define TLSR8258_PM_TICK_32K_CALIB  8000u
#define TLSR8258_PM_XTAL_STABLE_LOOPS 10u
#define TLSR8258_PM_XTAL_STABLE_NOPS 200u

struct tlsr8258_pm_r_delay_us {
	uint16_t deep_r_delay_us;
	uint16_t suspend_ret_r_delay_us;
};

struct tlsr8258_pm_early_wakeup_time_us {
	uint16_t suspend;
	uint16_t deep_ret;
	uint16_t deep;
	uint16_t min;
};

static volatile struct tlsr8258_pm_r_delay_us tlsr8258_pm_r_delay = {
	.deep_r_delay_us = 1000u,
	.suspend_ret_r_delay_us = 1000u,
};

static volatile struct tlsr8258_pm_early_wakeup_time_us tlsr8258_pm_early_wakeup = {
	.suspend = 0x0555u,
	.deep_ret = 0x044cu,
	.deep = 0x04d8u,
	.min = 0x06e5u,
};

static volatile uint32_t tlsr8258_pm_suspend_delay_us = 0x87u;
static volatile uint32_t tlsr8258_pm_xtal_stable_loopnum = TLSR8258_PM_XTAL_STABLE_LOOPS;
static volatile uint32_t tlsr8258_pm_xtal_stable_suspend_nopnum = TLSR8258_PM_XTAL_STABLE_NOPS;
static volatile uint32_t tlsr8258_pm_tick_cur;
static volatile uint32_t tlsr8258_pm_tick_32k_cur;
static volatile uint8_t tlsr8258_pm_long_suspend;
static volatile enum tlsr8258_pm_wakeup_reason tlsr8258_pm_last_reason =
	TLSR8258_PM_WAKEUP_NONE;
static volatile uint32_t tlsr8258_pm_last_raw_status;
static volatile uint32_t tlsr8258_pm_gpio_wakeup_mask;
static volatile bool tlsr8258_pm_idle_allowed;

static void tlsr8258_pm_vendor_clock_dly(uint32_t cycles)
{
	volatile uint32_t delay = 0u;

	while (delay < cycles) {
		delay++;
	}
}

static void tlsr8258_pm_analog_wait(void)
{
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr8258_pm_analog_read(uint8_t addr)
{
	unsigned int key = arch_irq_lock();
	uint8_t data;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	tlsr8258_pm_analog_wait();
	data = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;
	arch_irq_unlock(key);

	return data;
}

static void tlsr8258_pm_analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = arch_irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	tlsr8258_pm_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
	arch_irq_unlock(key);
}

static void tlsr8258_pm_set_default_timings(void)
{
	uint16_t suspend = (uint16_t)(tlsr8258_pm_r_delay.suspend_ret_r_delay_us + 0x00e6u +
					 tlsr8258_pm_suspend_delay_us);

	tlsr8258_pm_early_wakeup.suspend = suspend;
	tlsr8258_pm_early_wakeup.deep_ret =
		(uint16_t)(tlsr8258_pm_r_delay.suspend_ret_r_delay_us + 100u);
	tlsr8258_pm_early_wakeup.deep =
		(uint16_t)(tlsr8258_pm_r_delay.deep_r_delay_us + 240u);
	tlsr8258_pm_early_wakeup.min =
		(uint16_t)(MIN(tlsr8258_pm_early_wakeup.deep,
			       tlsr8258_pm_early_wakeup.suspend) + 0x0190u);
}

static enum tlsr8258_pm_wakeup_reason tlsr8258_pm_reason_from_status(uint32_t status)
{
	if ((status & TLSR8258_WAKEUP_STATUS_TIMER) != 0u) {
		return TLSR8258_PM_WAKEUP_TIMER;
	}

	if ((status & TLSR8258_WAKEUP_STATUS_PAD) != 0u) {
		return TLSR8258_PM_WAKEUP_GPIO;
	}

	if ((status & TLSR8258_WAKEUP_STATUS_CORE) != 0u) {
		return TLSR8258_PM_WAKEUP_CORE;
	}

	if ((status & TLSR8258_WAKEUP_STATUS_COMPARATOR) != 0u) {
		return TLSR8258_PM_WAKEUP_COMPARATOR;
	}

	return TLSR8258_PM_WAKEUP_NONE;
}

static uint32_t tlsr8258_pm_get_32k_tick(void)
{
	uint32_t t0 = 0u;
	uint32_t t1 = 0u;
	uint32_t n = 0u;

	while (true) {
		t0 = t1;
		t1 = tlsr8258_pm_analog_read(0x47u);
		t1 = (t1 << 8) | tlsr8258_pm_analog_read(0x46u);
		t1 = (t1 << 8) | tlsr8258_pm_analog_read(0x45u);
		t1 = (t1 << 8) | tlsr8258_pm_analog_read(0x43u);
		if (n != 0u) {
			if ((t1 - t0) < 2u) {
				return t1;
			}

			if ((t0 ^ t1) == 1u) {
				return t0;
			}
		}

		n++;
	}
}

static void tlsr8258_pm_wait_xtal_ready(void)
{
	for (uint32_t i = 0u; i < tlsr8258_pm_xtal_stable_loopnum; i++) {
		uint32_t start = TLSR8258_REG_SYSTEM_TICK;

		for (volatile uint32_t j = 0u; j <= 0x3bu; j++) {
		}

		if ((TLSR8258_REG_SYSTEM_TICK - start) > (20u * TLSR8258_PM_SYS_TICK_PER_US)) {
			return;
		}
	}
}

static bool tlsr8258_pm_wake_gate_ready(uint8_t wake44)
{
	return (wake44 & TLSR8258_WAKEUP_STATUS_ALL) == 0u;
}

static void __attribute__((section(".ram_code"))) tlsr8258_pm_sleep_start(void)
{
	volatile uint32_t *ret_slot =
		(volatile uint32_t *)(uintptr_t)(TLSR8258_PM_RET_ENTRY_BASE +
					 ((uint32_t)TLSR8258_REG_RET_SLOT_IDX << 8));
	uint32_t ret_slot_saved;

	tlsr8258_pm_analog_write(0x34u, 0x87u);
	TLSR8258_REG8(0x0080000du) = 0u;
	TLSR8258_REG8(0x0080000cu) = 0xb9u;

	for (volatile uint32_t i = 0u; i <= 1u; i++) {
	}

	TLSR8258_REG8(0x0080000du) = 1u;
	TLSR8258_REG8(0x008005a1u) = 0u;
	tlsr8258_pm_analog_write(0x82u, 0x0cu);

	ret_slot_saved = *ret_slot;
	*ret_slot = TLSR8258_PM_RET_SLOT_NOP;
	start_suspend();
	*ret_slot = ret_slot_saved;

	tlsr8258_pm_analog_write(0x82u, 0x64u);
	TLSR8258_REG8(0x008005a1u) = 0x0fu;
	TLSR8258_REG8(0x0080000du) = 0u;
	TLSR8258_REG8(0x0080000cu) = 0xabu;

	for (volatile uint32_t i = 0u; i <= 1u; i++) {
	}

	TLSR8258_REG8(0x0080000du) = 1u;
	tlsr8258_pm_analog_write(0x34u, 0x80u);

	for (volatile uint32_t i = 0u; i <= tlsr8258_pm_xtal_stable_suspend_nopnum; i++) {
	}
}

static uint32_t tlsr8258_pm_current_wakeup_sources(void)
{
	uint32_t wakeup_src = 0u;

	if (IS_ENABLED(CONFIG_TLSR8258_PM_TIMER_WAKEUP)) {
		wakeup_src |= TLSR8258_PM_WAKEUP_TIMER_BITS;
	}

	if (IS_ENABLED(CONFIG_TLSR8258_PM_GPIO_WAKEUP) && tlsr8258_pm_gpio_wakeup_mask != 0u) {
		wakeup_src |= TLSR8258_PM_WAKEUP_PAD_BITS;
	}

	return wakeup_src;
}

static uint32_t tlsr8258_pm_stall_wakeup_sources(void)
{
	uint32_t wakeup_src = TLSR8258_WAKEUP_SRC_TMR1;

	if (IS_ENABLED(CONFIG_TLSR8258_PM_GPIO_WAKEUP) && tlsr8258_pm_gpio_wakeup_mask != 0u) {
		wakeup_src |= TLSR8258_WAKEUP_SRC_GPIO;
		wakeup_src |= TLSR8258_WAKEUP_SRC_GPIO_RM;
	}

	return wakeup_src;
}

static uint32_t tlsr8258_pm_suspend_stall(uint32_t duration_ms)
{
	unsigned int irq_key = arch_irq_lock();
	uint32_t interval_us = MAX(duration_ms, 1u) * 1000u;
	uint32_t wakeup_src = tlsr8258_pm_stall_wakeup_sources();

	TLSR8258_REG_TMR1_TICK = 0u;
	TLSR8258_REG_TMR1_CAPT = interval_us * TLSR8258_PM_SYS_TICK_PER_US;
	*TLSR8258_REG_TMR_STA = TLSR8258_FLD_TMR_STA_TMR1;
	TLSR8258_REG_TMR_CTRL8 &= (uint8_t)~TLSR8258_FLD_TMR1_MODE;
	TLSR8258_REG_TMR_CTRL8 |= TLSR8258_FLD_TMR1_EN;

	TLSR8258_REG_MCU_WAKEUP_MASK |= wakeup_src;
	*TLSR8258_REG_IRQ_MASK &= ~(TLSR8258_FLD_IRQ_TMR1_EN | TLSR8258_FLD_IRQ_ZB_RT_EN);
	TLSR8258_REG_PWDN_CTRL = TLSR8258_FLD_PWDN_CTRL_SLEEP;

	compiler_barrier();
	compiler_barrier();

	TLSR8258_REG_TMR1_TICK = 0u;
	TLSR8258_REG_TMR_CTRL8 &= (uint8_t)~TLSR8258_FLD_TMR1_EN;

	uint32_t status = TLSR8258_REG_PM_MISC_DUMMY;

	*TLSR8258_REG_TMR_STA = TLSR8258_FLD_TMR_STA_TMR1;
	TLSR8258_REG_RF_IRQ_STATUS = 0xffffu;

	arch_irq_unlock(irq_key);

	if (status != 0u) {
		return status | TLSR8258_STATUS_ENTER_SUSPEND;
	}

	return TLSR8258_STATUS_GPIO_ERR_NO_ENTER_PM;
}

static uint32_t tlsr8258_pm_suspend_rc32k(uint32_t wakeup_tick, uint32_t wakeup_src)
{
	unsigned int irq_key = arch_irq_lock();
	uint32_t t0 = TLSR8258_REG_SYSTEM_TICK;
	bool timer_wakeup = (wakeup_src & TLSR8258_PM_WAKEUP_TIMER_BITS) != 0u;
	uint8_t wakeup_src_u8 = (uint8_t)wakeup_src;
	uint16_t calib = TLSR8258_PM_TICK_32K_CALIB;

	if (timer_wakeup) {
		uint32_t dt = wakeup_tick - t0;
		uint32_t early = (uint32_t)tlsr8258_pm_early_wakeup.min << 4;

		if (dt > 0xE0000000u) {
			arch_irq_unlock(irq_key);
			return tlsr8258_pm_analog_read(TLSR8258_AREG_WAKEUP_STATUS) &
			       TLSR8258_WAKEUP_STATUS_ALL;
		}

		if (dt >= early) {
			tlsr8258_pm_long_suspend = (dt > (0xffu << 20)) ? 1u : 0u;
		} else {
			uint8_t st;

			tlsr8258_pm_analog_write(TLSR8258_AREG_WAKEUP_STATUS,
						 TLSR8258_WAKEUP_STATUS_ALL);
			do {
				st = tlsr8258_pm_analog_read(TLSR8258_AREG_WAKEUP_STATUS) &
				     TLSR8258_WAKEUP_STATUS_ALL;
			} while (((TLSR8258_REG_SYSTEM_TICK - t0) < dt) && (st == 0u));

			arch_irq_unlock(irq_key);
			return st;
		}
	}

	tlsr8258_pm_tick_cur = TLSR8258_REG_SYSTEM_TICK + (0x8cu << 2);
	tlsr8258_pm_tick_32k_cur = tlsr8258_pm_get_32k_tick();

	uint32_t target =
		wakeup_tick - ((uint32_t)tlsr8258_pm_early_wakeup.suspend << 4);
	uint8_t bak66 = TLSR8258_REG_CLK_SEL;
	uint32_t d = target - tlsr8258_pm_tick_cur;
	uint32_t wake_tick;

	tlsr8258_pm_analog_write(TLSR8258_AREG_0X26, wakeup_src_u8);
	tlsr8258_pm_analog_write(TLSR8258_AREG_WAKEUP_STATUS, TLSR8258_WAKEUP_STATUS_ALL);
	TLSR8258_REG_CLK_SEL = 0u;

	tlsr8258_pm_analog_write(TLSR8258_AREG_0X04, 0x48u);
	tlsr8258_pm_analog_write(TLSR8258_AREG_0X7E, 0x00u);
	tlsr8258_pm_analog_write(TLSR8258_AREG_0X2B, 0x5eu);
	tlsr8258_pm_analog_write(TLSR8258_AREG_0X2C,
				 (uint8_t)(0x96u | 0x16u | (timer_wakeup ? 1u : 0u)));
	tlsr8258_pm_analog_write(TLSR8258_AREG_LDO_SETTING1,
				 (tlsr8258_pm_analog_read(TLSR8258_AREG_LDO_SETTING1) &
				  (uint8_t)~0x07u) |
					 4u);
	TLSR8258_REG_PM_RET_SRAM_CTRL = 0x08u;
	tlsr8258_pm_analog_write(TLSR8258_AREG_PM_STATUS, 1u);
	tlsr8258_pm_analog_write(TLSR8258_AREG_0X20,
				 (uint8_t)(0x7fu - ((0xfa00u + (calib >> 1)) / calib)));
	tlsr8258_pm_analog_write(
		TLSR8258_AREG_0X1F,
		(uint8_t)((((uint32_t)tlsr8258_pm_r_delay.suspend_ret_r_delay_us << 7) +
			   (calib >> 1)) /
			  calib));

	if (tlsr8258_pm_long_suspend != 0u) {
		wake_tick = target - ((d / calib) << 4) + tlsr8258_pm_tick_32k_cur;
	} else {
		wake_tick = target - (((d << 4) + (calib >> 1)) / calib) +
			    tlsr8258_pm_tick_32k_cur;
	}

	TLSR8258_REG_SYSTEM_TICK_MODE = 0x2cu;
	TLSR8258_REG_SYSTEM_32K_TICK_CAL = wake_tick;
	TLSR8258_REG_SYSTEM_TICK_CTRL = 0x08u;
	tlsr8258_pm_vendor_clock_dly(10u);
	tlsr8258_pm_vendor_clock_dly(6u);
	while ((TLSR8258_REG_SYSTEM_TICK_CTRL & 0x08u) != 0u) {
	}
	TLSR8258_REG_SYSTEM_TICK_MODE = 0x20u;

	if (tlsr8258_pm_wake_gate_ready(
		    tlsr8258_pm_analog_read(TLSR8258_AREG_WAKEUP_STATUS))) {
		tlsr8258_pm_sleep_start();
	}

	{
		uint32_t t32 = tlsr8258_pm_get_32k_tick();

		if (tlsr8258_pm_long_suspend != 0u) {
			tlsr8258_pm_tick_cur +=
				((t32 - tlsr8258_pm_tick_32k_cur) >> 4) * calib;
		} else {
			tlsr8258_pm_tick_cur +=
				((t32 - tlsr8258_pm_tick_32k_cur) * calib) >> 4;
		}

		tlsr8258_pm_tick_32k_cur = tlsr8258_pm_tick_cur + (20u * 16u);
	}

	TLSR8258_REG_SYSTEM_TICK_MODE = 0x00u;
	tlsr8258_pm_vendor_clock_dly(6u);
	TLSR8258_REG_SYSTEM_TICK_MODE = 0x92u;
	tlsr8258_pm_vendor_clock_dly(4u);
	TLSR8258_REG_SYSTEM_TICK_CTRL = 0x01u;

	tlsr8258_pm_wait_xtal_ready();
	TLSR8258_REG_CLK_SEL = bak66;

	uint32_t st = tlsr8258_pm_analog_read(TLSR8258_AREG_WAKEUP_STATUS);

	arch_irq_unlock(irq_key);

	if (st != 0u) {
		return st | TLSR8258_STATUS_ENTER_SUSPEND;
	}

	return TLSR8258_STATUS_GPIO_ERR_NO_ENTER_PM;
}

static int tlsr8258_pm_enter_suspend_to_idle(void)
{
	uint32_t wakeup_tick = TLSR8258_REG_SYSTEM_TICK_IRQ;
	uint32_t status = tlsr8258_pm_suspend_rc32k(wakeup_tick, tlsr8258_pm_current_wakeup_sources());

	tlsr8258_pm_last_raw_status = status;
	tlsr8258_pm_last_reason = tlsr8258_pm_reason_from_status(status);

	return (status & TLSR8258_STATUS_GPIO_ERR_NO_ENTER_PM) != 0u ? -EIO : 0;
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(substate_id);

	switch (state) {
	case PM_STATE_SUSPEND_TO_IDLE:
		if (tlsr8258_pm_idle_allowed) {
			(void)tlsr8258_pm_enter_suspend_to_idle();
		}
		break;
	default:
		break;
	}
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	arch_irq_unlock(1u);
}

int tlsr8258_pm_suspend_for_ms(uint32_t duration_ms)
{
	uint32_t status = tlsr8258_pm_suspend_stall(duration_ms);

	tlsr8258_pm_last_raw_status = status;
	tlsr8258_pm_last_reason = tlsr8258_pm_reason_from_status(status);

	if ((status & TLSR8258_STATUS_GPIO_ERR_NO_ENTER_PM) != 0u) {
		return -EIO;
	}

	return 0;
}

int tlsr8258_pm_deep_retention_for_ms(uint32_t duration_ms)
{
	ARG_UNUSED(duration_ms);

	return -ENOTSUP;
}

int tlsr8258_pm_shutdown_for_ms(uint32_t duration_ms)
{
	ARG_UNUSED(duration_ms);

	return -ENOTSUP;
}

int tlsr8258_pm_configure_gpio_wakeup(uint8_t port, uint8_t pin, bool active_low, bool enable)
{
	uint8_t bit;
	uint8_t pol_reg;
	uint8_t en_reg;
	uint8_t value;
	uint32_t key_bit;

	if (port > TLSR8258_PM_GPIO_PORT_D || pin > 7u) {
		return -EINVAL;
	}

	bit = (uint8_t)BIT(pin);
	key_bit = BIT((port * 8u) + pin);
	pol_reg = (uint8_t)(0x21u + port);
	en_reg = (uint8_t)(0x27u + port);

	value = tlsr8258_pm_analog_read(pol_reg);
	if (active_low) {
		value |= bit;
	} else {
		value &= (uint8_t)~bit;
	}
	tlsr8258_pm_analog_write(pol_reg, value);

	value = tlsr8258_pm_analog_read(en_reg);
	if (enable) {
		value |= bit;
		tlsr8258_pm_gpio_wakeup_mask |= key_bit;
	} else {
		value &= (uint8_t)~bit;
		tlsr8258_pm_gpio_wakeup_mask &= ~key_bit;
	}
	tlsr8258_pm_analog_write(en_reg, value);
	tlsr8258_pm_analog_write(TLSR8258_AREG_WAKEUP_STATUS, TLSR8258_WAKEUP_STATUS_ALL);

	return 0;
}

enum tlsr8258_pm_wakeup_reason tlsr8258_pm_get_wakeup_reason(void)
{
	return tlsr8258_pm_last_reason;
}

uint32_t tlsr8258_pm_get_wakeup_raw_status(void)
{
	return tlsr8258_pm_last_raw_status;
}

static int tlsr8258_pm_init(void)
{
	tlsr8258_pm_set_default_timings();
	TLSR8258_REG_SYSTEM_32K_TICK_RD = 0u;
	TLSR8258_REG_GPIO_WAKEUP_IRQ |=
		(TLSR8258_FLD_GPIO_CORE_WAKEUP_EN | TLSR8258_FLD_GPIO_CORE_INTERRUPT_EN);
	tlsr8258_pm_idle_allowed = false;
	tlsr8258_pm_last_raw_status =
		tlsr8258_pm_analog_read(TLSR8258_AREG_WAKEUP_STATUS) &
		TLSR8258_WAKEUP_STATUS_ALL;
	tlsr8258_pm_last_reason =
		tlsr8258_pm_reason_from_status(tlsr8258_pm_last_raw_status);

	return 0;
}

SYS_INIT(tlsr8258_pm_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
