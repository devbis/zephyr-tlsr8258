/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * PM_STATE_SOFT_OFF on the BL70x: hibernate (HBN level 0) with a wakeup from
 * the RTC. Only the RTC and the 4 KB retention RAM stay powered, so the
 * wakeup is a reset that starts from the boot ROM. Everything the firmware
 * needs after the wakeup has to live in the retention RAM (see
 * zephyr,retained-ram) or in the flash.
 *
 * The state is timed by the kernel's next timeout: the idle loop has armed
 * mtimecmp before it calls pm_state_set(), so the time left until it is the
 * time to sleep. The state stays locked until code that can restore itself
 * releases it with pm_policy_state_lock_put().
 */

#include <string.h>

#include <zephyr/arch/riscv/arch.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include <bflb_soc.h>
#include <hbn_reg.h>

#define MTIME_ADDR    DT_REG_ADDR_BY_NAME(DT_NODELABEL(mtimer), mtime)
#define MTIMECMP_ADDR DT_REG_ADDR_BY_NAME(DT_NODELABEL(mtimer), mtimecmp)
#define MTIME_HZ      DT_PROP(DT_PATH(cpus), timebase_frequency)

#define XIP_BASE      DT_REG_ADDR(DT_NODELABEL(sf_bank1))

#define SOFT_OFF_NODE DT_NODELABEL(bl70x_soft_off)
#define EXIT_LATENCY_US DT_PROP(SOFT_OFF_NODE, exit_latency_us)

/*
 * The boot ROM API table: function addresses at fixed indices. These are the
 * ones the hibernate sequence needs; the indices are those of the ROM.
 */
#define ROM_API                 ((volatile uint32_t *)0x21018800)
#define ROM_AON_POWER_OFF_XTAL  8
#define ROM_GLB_POWER_OFF_DLL   28
#define ROM_HBN_TRIM_RC32K      74
#define ROM_SFLASH_POWERDOWN    107
#define ROM_SFLASH_RESET_CREAD  113
#define ROM_SF_CTRL_SET_OWNER   133

/* SPI_Flash_Cfg_Type is the 84 byte block at offset 12 of the boot header. */
#define FLASH_CFG_OFFSET 12U
#define FLASH_CFG_LEN    84U

#define SF_CTRL_OWNER_SAHB 0U

#define HBN_STATUS_ENTER_FLAG 0x4e424845U
#define HBN_LDO_LEVEL_1P00V   8U
/* Word in the retention RAM the vendor boot code reads as "RTC is enabled". */
#define RETRAM_RTC_ENABLE     0x40010FFCU

/* RC32K nominal frequency, used until the awake time is long enough to measure it. */
#define RTC_HZ_NOMINAL 32700U
#define RTC_CAL_MIN_US 10000U
/* The RTC compare is 40 bits wide. */
#define RTC_CYCLES_MAX 0xffffffff00ULL

static uint64_t boot_mtime;
static uint64_t boot_rtc;
static bool boot_rtc_valid;

static uint64_t mtime_read(void)
{
	uint32_t hi, lo, hi2;

	do {
		hi = sys_read32(MTIME_ADDR + 4U);
		lo = sys_read32(MTIME_ADDR);
		hi2 = sys_read32(MTIME_ADDR + 4U);
	} while (hi != hi2);

	return ((uint64_t)hi << 32) | lo;
}

static uint64_t mtimecmp_read(void)
{
	return ((uint64_t)sys_read32(MTIMECMP_ADDR + 4U) << 32) | sys_read32(MTIMECMP_ADDR);
}

/* The RTC counter is latched by a rising edge of a bit in HBN_RTC_TIME_H. */
static uint64_t rtc_read(void)
{
	uint32_t t, hi, lo, hi2, lo2;
	uint64_t a, b;

	do {
		t = sys_read32(HBN_BASE + HBN_RTC_TIME_H_OFFSET);
		sys_write32(t | BIT(HBN_RTC_TIME_LATCH_POS), HBN_BASE + HBN_RTC_TIME_H_OFFSET);
		sys_write32(t & ~BIT(HBN_RTC_TIME_LATCH_POS), HBN_BASE + HBN_RTC_TIME_H_OFFSET);
		lo = sys_read32(HBN_BASE + HBN_RTC_TIME_L_OFFSET);
		hi = sys_read32(HBN_BASE + HBN_RTC_TIME_H_OFFSET) & 0xffU;
		a = ((uint64_t)hi << 32) | lo;

		t = sys_read32(HBN_BASE + HBN_RTC_TIME_H_OFFSET);
		sys_write32(t | BIT(HBN_RTC_TIME_LATCH_POS), HBN_BASE + HBN_RTC_TIME_H_OFFSET);
		sys_write32(t & ~BIT(HBN_RTC_TIME_LATCH_POS), HBN_BASE + HBN_RTC_TIME_H_OFFSET);
		lo2 = sys_read32(HBN_BASE + HBN_RTC_TIME_L_OFFSET);
		hi2 = sys_read32(HBN_BASE + HBN_RTC_TIME_H_OFFSET) & 0xffU;
		b = ((uint64_t)hi2 << 32) | lo2;
	} while (b < a);

	return b;
}

/*
 * Runs from RAM: the flash is powered down before the last steps. It does not
 * return; the RTC compare resets the SoC.
 */
static __ramfunc void hbn_enter(uint64_t cycles, uint8_t *flash_cfg)
{
	void (*trim_rc32k)(void) = (void (*)(void))ROM_API[ROM_HBN_TRIM_RC32K];
	void (*xtal_off)(void) = (void (*)(void))ROM_API[ROM_AON_POWER_OFF_XTAL];
	void (*dll_off)(void) = (void (*)(void))ROM_API[ROM_GLB_POWER_OFF_DLL];
	void (*sf_owner)(uint32_t) = (void (*)(uint32_t))ROM_API[ROM_SF_CTRL_SET_OWNER];
	void (*sf_cread_reset)(void *) = (void (*)(void *))ROM_API[ROM_SFLASH_RESET_CREAD];
	void (*sf_powerdown)(void) = (void (*)(void))ROM_API[ROM_SFLASH_POWERDOWN];
	uint32_t t, lo, hi;
	uint64_t compare;

	/* The 32 kHz clock is the RC32K; the crystal oscillator is not used. */
	trim_rc32k();
	t = sys_read32(HBN_BASE + HBN_GLB_OFFSET);
	t = (t & HBN_F32K_SEL_UMSK) | (0U << HBN_F32K_SEL_POS);
	t |= BIT(HBN_PU_RC32K_POS);
	sys_write32(t, HBN_BASE + HBN_GLB_OFFSET);
	for (volatile uint32_t i = 0; i < 20000U; i++) {
	}
	t = sys_read32(HBN_BASE + HBN_XTAL32K_OFFSET);
	t &= ~(BIT(HBN_PU_XTAL32K_POS) | BIT(HBN_PU_XTAL32K_BUF_POS));
	sys_write32(t, HBN_BASE + HBN_XTAL32K_OFFSET);

	/* No pin wakeup and no pulls. */
	t = sys_read32(HBN_BASE + HBN_IRQ_MODE_OFFSET);
	t &= ~BIT(HBN_REG_EN_HW_PU_PD_POS);
	t |= HBN_PIN_WAKEUP_MASK_MSK;
	t &= HBN_REG_AON_PAD_IE_SMT_UMSK;
	sys_write32(t, HBN_BASE + HBN_IRQ_MODE_OFFSET);

	/* RTC wakeup: the counter value now plus the sleep time. */
	sys_write32(1U, RETRAM_RTC_ENABLE);
	t = sys_read32(HBN_BASE + HBN_CTL_OFFSET);
	sys_write32(t & ~1U, HBN_BASE + HBN_CTL_OFFSET);
	t = sys_read32(HBN_BASE + HBN_RTC_TIME_H_OFFSET);
	sys_write32(t | BIT(HBN_RTC_TIME_LATCH_POS), HBN_BASE + HBN_RTC_TIME_H_OFFSET);
	sys_write32(t & ~BIT(HBN_RTC_TIME_LATCH_POS), HBN_BASE + HBN_RTC_TIME_H_OFFSET);
	lo = sys_read32(HBN_BASE + HBN_RTC_TIME_L_OFFSET);
	hi = sys_read32(HBN_BASE + HBN_RTC_TIME_H_OFFSET) & 0xffU;
	compare = ((uint64_t)hi << 32) + lo + cycles;
	sys_write32((uint32_t)compare, HBN_BASE + HBN_TIME_L_OFFSET);
	sys_write32((uint32_t)(compare >> 32) & 0xffU, HBN_BASE + HBN_TIME_H_OFFSET);
	t = sys_read32(HBN_BASE + HBN_CTL_OFFSET);
	t = (t & HBN_RTC_DLY_OPTION_UMSK) | (1U << HBN_RTC_DLY_OPTION_POS);
	t |= (1U << 1); /* compare bits 0..39 */
	sys_write32(t, HBN_BASE + HBN_CTL_OFFSET);
	t = sys_read32(HBN_BASE + HBN_CTL_OFFSET);
	sys_write32(t | 1U, HBN_BASE + HBN_CTL_OFFSET);

	/* Flash into deep power-down, then the clocks. */
	sf_owner(SF_CTRL_OWNER_SAHB);
	sf_cread_reset(flash_cfg);
	sf_powerdown();

	t = sys_read32(HBN_BASE + HBN_GLB_OFFSET);
	t &= HBN_ROOT_CLK_SEL_UMSK; /* RC32M */
	sys_write32(t, HBN_BASE + HBN_GLB_OFFSET);
	dll_off();
	xtal_off();

	/* Hibernate level 0: the HBN core and its RAM keep their power. */
	t = sys_read32(HBN_BASE + HBN_CTL_OFFSET);
	t = (t & HBN_LDO11_AON_VOUT_SEL_UMSK) | (HBN_LDO_LEVEL_1P00V << HBN_LDO11_AON_VOUT_SEL_POS);
	t = (t & HBN_LDO11_RT_VOUT_SEL_UMSK) | (HBN_LDO_LEVEL_1P00V << HBN_LDO11_RT_VOUT_SEL_POS);
	sys_write32(t, HBN_BASE + HBN_CTL_OFFSET);
	sys_write32(HBN_STATUS_ENTER_FLAG, HBN_BASE + HBN_RSV0_OFFSET);
	t = sys_read32(HBN_BASE + HBN_CTL_OFFSET);
	t &= ~BIT(HBN_PWRDN_HBN_CORE_POS);
	t &= ~BIT(HBN_PWRDN_HBN_RTC_POS);
	t &= ~BIT(HBN_PWR_ON_OPTION_POS);
	sys_write32(t, HBN_BASE + HBN_CTL_OFFSET);
	t = sys_read32(HBN_BASE + HBN_CTL_OFFSET);
	sys_write32(t | BIT(HBN_MODE_POS), HBN_BASE + HBN_CTL_OFFSET);

	for (;;) {
	}
}

/* RTC cycles for a time in microseconds, from the clock measured while awake. */
static uint64_t rtc_cycles_from_us(uint64_t us)
{
	uint64_t hz = RTC_HZ_NOMINAL;

	if (boot_rtc_valid) {
		uint64_t dm = mtime_read() - boot_mtime;
		uint64_t dr = rtc_read() - boot_rtc;

		if (dm >= ((uint64_t)RTC_CAL_MIN_US * MTIME_HZ) / 1000000ULL && dr != 0U) {
			hz = (dr * MTIME_HZ) / dm;
		}
	}

	return (us * hz) / 1000000ULL;
}

void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	uint8_t flash_cfg[FLASH_CFG_LEN];
	uint64_t cmp, now, us, cycles;

	ARG_UNUSED(substate_id);

	if (state != PM_STATE_SOFT_OFF) {
		return;
	}

	cmp = mtimecmp_read();
	now = mtime_read();
	us = (cmp > now) ? ((cmp - now) * 1000000ULL) / MTIME_HZ : 0U;
	us = (us > EXIT_LATENCY_US) ? (us - EXIT_LATENCY_US) : 0U;
	cycles = rtc_cycles_from_us(us);
	if (cycles > RTC_CYCLES_MAX) {
		cycles = RTC_CYCLES_MAX;
	}

	memcpy(flash_cfg, (const void *)(XIP_BASE + FLASH_CFG_OFFSET), sizeof(flash_cfg));
	if (IS_ENABLED(CONFIG_UART_CONSOLE)) {
		/* Let the last characters leave the UART. */
		k_busy_wait(3000);
	}

	hbn_enter(cycles, flash_cfg);
}

void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	/* Only reached if the state was not entered. */
	arch_irq_unlock(MSTATUS_IEN);
}

/*
 * Keep the RTC counting from the start and remember the time and the counter
 * together, so the awake time before the first sleep calibrates the RC32K.
 */
static int bl70x_pm_init(void)
{
	uint32_t t = sys_read32(HBN_BASE + HBN_CTL_OFFSET);

	if ((t & 1U) == 0U) {
		sys_write32(t | 1U, HBN_BASE + HBN_CTL_OFFSET);
		boot_rtc = 0U;
	} else {
		boot_rtc = rtc_read();
	}
	boot_mtime = mtime_read();
	boot_rtc_valid = true;

	return 0;
}

SYS_INIT(bl70x_pm_init, PRE_KERNEL_1, 0);

/*
 * The wakeup is a reset, so entering the state is never transparent to the
 * running firmware: hold it back until code that can restore itself asks.
 */
static int bl70x_pm_soft_off_lock_init(void)
{
	pm_policy_state_lock_get(PM_STATE_SOFT_OFF, PM_ALL_SUBSTATES);

	return 0;
}

SYS_INIT(bl70x_pm_soft_off_lock_init, POST_KERNEL, 0);
