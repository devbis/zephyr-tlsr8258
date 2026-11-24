/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/linker/sections.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "soc.h"

/*
 * Suspend to RAM: the chip sleeps with its RAM retained and, when the RTC or a wake pad wakes it,
 * the boot ROM runs the function in the slot 62 of the jump table (WAKEUP_PROCESS) instead of its
 * own, which belongs to the BLE controller of the SDK. That function restores what the sleep
 * lost, the clock, the cache of the flash, the clock gates, the NVIC and the SysTick, and the
 * registers of the program as longjmp() does, so phy62x2_suspend() returns to its caller. The
 * peripherals lose their registers in the sleep and the drivers have to set them again.
 *
 * The sequence was read from a dump of the boot ROM and from the SDK's hal_pwrmgr_enter_standby(),
 * and has not been run: the sleep is entered through enter_sleep_off_mode() of the ROM, which writes
 * the key of the sleep to AON->PWRSLP.
 */
#define ROM_ENTER_SLEEP_OFF_MODE ((void (*)(uint32_t))0x0000afa1U)
#define SYSTEM_SLEEP_MODE 1U

#define JUMP_TABLE_BASE 0x1fff0000U
#define JUMP_WAKEUP_PROCESS 62U

#define AON_PMCTL2_0 0x4000f01cU
#define AON_SLEEP_R0 0x4000f0c0U
#define AON_RTC_CTL 0x4000f024U
#define AON_RTC_CNT 0x4000f028U
#define AON_RTC_CC0 0x4000f02cU
#define PMCTL2_0_SW_CONTROL BIT(6)
#define PMCTL2_0_RETENTION_MASK GENMASK(21, 17)
#define PMCTL2_0_RETENTION_SHIFT 17U
/* The bits that config_RTC() of the ROM sets in RTCCTL when it arms the first comparator */
#define RTC_CTL_ARM_CC0 (BIT(15) | BIT(18) | BIT(20))
/* The three blocks of the SRAM, 64 KiB: the Zephyr image is spread over all of them */
#define RETENTION_ALL_SRAM 0x7U
/* The value that the SDK writes before it enters the sleep with the RAM retained */
#define SLEEP_R0_POWER_OFF 2U

#define PCR_SW_CLK 0x40000008U
#define PCR_SW_CLK1 0x40000014U
#define SCB_SHPR2 0xe000ed1cU
#define SCB_SHPR3 0xe000ed20U
#define NVIC_ISER 0xe000e100U
#define NVIC_ICER 0xe000e180U
#define NVIC_ICPR 0xe000e280U
#define NVIC_IPR(n) (0xe000e400U + 4U * (n))
#define NVIC_IPR_WORDS 8U
#define SYST_CSR 0xe000e010U
#define SYST_RVR 0xe000e014U
#define SYST_CVR 0xe000e018U

/* The registers of the program, see suspend.S, then what the sleep lost */
struct suspend_state {
	uint32_t regs[13];
	uint32_t sw_clk;
	uint32_t sw_clk1;
	uint32_t nvic_enabled;
	uint32_t nvic_priority[NVIC_IPR_WORDS];
	uint32_t shpr2;
	uint32_t shpr3;
	uint32_t systick_csr;
	uint32_t systick_rvr;
};

/* Returns twice, like setjmp(): the compiler must not keep values in registers across it */
extern int phy62x2_ctx_save(uint32_t *ctx) __attribute__((returns_twice));
extern void phy62x2_ctx_restore(const uint32_t *ctx);

static struct suspend_state state;

/* The wakeup function of the jump table: it runs on the stack of the ROM, with the clock reset */
static __ramfunc void phy62x2_resume(void)
{
	__asm__ volatile("cpsid i" ::: "memory");

	/* The jump table is retained, clear the slot so that the ROM's own wakeup is used next */
	sys_write32(0U, JUMP_TABLE_BASE + 4U * JUMP_WAKEUP_PROCESS);

	(void)phy62x2_clock_init();
	sys_write32(state.sw_clk, PCR_SW_CLK);
	sys_write32(state.sw_clk1, PCR_SW_CLK1);
	phy62x2_xip_cache_enable();

	sys_write32(UINT32_MAX, NVIC_ICER);
	sys_write32(UINT32_MAX, NVIC_ICPR);
	for (uint32_t i = 0U; i < NVIC_IPR_WORDS; i++) {
		sys_write32(state.nvic_priority[i], NVIC_IPR(i));
	}
	sys_write32(state.shpr2, SCB_SHPR2);
	sys_write32(state.shpr3, SCB_SHPR3);
	sys_write32(state.nvic_enabled, NVIC_ISER);

	/* The counter of the SysTick starts again from its reload value */
	sys_write32(0U, SYST_CSR);
	sys_write32(state.systick_rvr, SYST_RVR);
	sys_write32(0U, SYST_CVR);
	sys_write32(state.systick_csr, SYST_CSR);

	phy62x2_ctx_restore(state.regs);
}

static __ramfunc void phy62x2_sleep_enter(uint32_t rtc_ticks)
{
	volatile uint32_t *const pmctl2_0 = (volatile uint32_t *)AON_PMCTL2_0;

	/* The comparator of the RTC that wakes the chip, as config_RTC() arms it */
	if (rtc_ticks != 0U) {
		sys_write32(sys_read32(AON_RTC_CNT) + rtc_ticks, AON_RTC_CC0);
		sys_write32(sys_read32(AON_RTC_CTL) | RTC_CTL_ARM_CC0, AON_RTC_CTL);
	}

	*pmctl2_0 &= ~PMCTL2_0_SW_CONTROL;
	*pmctl2_0 = (*pmctl2_0 & ~PMCTL2_0_RETENTION_MASK) |
		    (RETENTION_ALL_SRAM << PMCTL2_0_RETENTION_SHIFT);
	sys_write32(SLEEP_R0_POWER_OFF, AON_SLEEP_R0);

	ROM_ENTER_SLEEP_OFF_MODE(SYSTEM_SLEEP_MODE);

	for (;;) {
	}
}

int phy62x2_suspend(uint32_t rtc_ticks)
{
	const unsigned int key = irq_lock();

	state.sw_clk = sys_read32(PCR_SW_CLK);
	state.sw_clk1 = sys_read32(PCR_SW_CLK1);
	state.nvic_enabled = sys_read32(NVIC_ISER);
	for (uint32_t i = 0U; i < NVIC_IPR_WORDS; i++) {
		state.nvic_priority[i] = sys_read32(NVIC_IPR(i));
	}
	state.shpr2 = sys_read32(SCB_SHPR2);
	state.shpr3 = sys_read32(SCB_SHPR3);
	state.systick_csr = sys_read32(SYST_CSR);
	state.systick_rvr = sys_read32(SYST_RVR);

	phy62x2_wakeup_arm(true);
	sys_write32((uint32_t)(uintptr_t)phy62x2_resume, JUMP_TABLE_BASE + 4U * JUMP_WAKEUP_PROCESS);

	if (phy62x2_ctx_save(state.regs) == 0) {
		phy62x2_sleep_enter(rtc_ticks);
	}

	/* The program continues here after the wakeup, with the interrupts locked as they were */
	irq_unlock(key);

	return 0;
}
