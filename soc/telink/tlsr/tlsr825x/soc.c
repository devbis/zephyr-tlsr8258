/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/devicetree.h>

#include <zephyr/platform/hooks.h>
#include <zephyr/sys/util.h>

#define TLSR8258_REG_RST_CLK0 (*(volatile uint32_t *)0x00800060u)
#define TLSR8258_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR8258_REG_TMR_CTRL  (*(volatile uint32_t *)0x00800620u)
#define TLSR8258_FLD_TMR_WD_EN BIT(23)

#define TLSR8258_REG_ANA_ADDR (*(volatile uint8_t *)0x008000b8u)
#define TLSR8258_REG_ANA_DATA (*(volatile uint8_t *)0x008000b9u)
#define TLSR8258_REG_ANA_CTRL (*(volatile uint8_t *)0x008000bau)

#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

#define TLSR8258_REG_CLK_SEL          (*(volatile uint8_t *)0x00800066u)
#define TLSR8258_REG_SYSTEM_TICK      (*(volatile uint32_t *)0x00800740u)
#define TLSR8258_REG_SYSTEM_TICK_MODE (*(volatile uint8_t *)0x0080074cu)
#define TLSR8258_REG_SYSTEM_TICK_CTRL (*(volatile uint8_t *)0x0080074fu)
#define TLSR8258_FLD_SYSTEM_TICK_START BIT(0)
#define TLSR8258_FLD_SYSTEM_TICK_16M   BIT(4)

/*
 * The system timer counts the stimer_clk node, 16 MHz whatever the CPU clock is;
 * the tick mode written below is the 16 MHz one.
 */
BUILD_ASSERT(DT_PROP(DT_NODELABEL(stimer_clk), clock_frequency) == 16000000,
	     "stimer_clk must be 16 MHz");
#define TLSR8258_TICKS_PER_US (DT_PROP(DT_NODELABEL(stimer_clk), clock_frequency) / 1000000u)
/* Time the crystal gets to show that it runs, in microseconds. */
#define TLSR8258_XTAL_READY_US 20u
#define TLSR8258_XTAL_READY_SPINS 100000u

/* Analog register that holds the flash supply trim; the vendor raises it at 48 MHz. */
#define TLSR8258_AREG_FLASH_VOLTAGE 0x0cu
#define TLSR8258_FLASH_VOLTAGE_48M  0xc6u

/*
 * The system clock is the frequency of the sys_clk devicetree node, and the
 * drivers divide it to get their rates, so what the SoC programs here has to be
 * what the node says. The vendor SDK offers the same three frequencies from the
 * crystal on this SoC.
 */
#define TLSR8258_SYS_CLK_HZ DT_PROP(DT_NODELABEL(sys_clk), clock_frequency)

#if TLSR8258_SYS_CLK_HZ == 16000000
#define TLSR8258_SYS_CLK_SEL 0x43u
#elif TLSR8258_SYS_CLK_HZ == 24000000
#define TLSR8258_SYS_CLK_SEL 0x42u
#elif TLSR8258_SYS_CLK_HZ == 48000000
#define TLSR8258_SYS_CLK_SEL 0x20u
#else
#error "sys_clk must be 16, 24 or 48 MHz"
#endif

/* Analog register values of the vendor cpu_wakeup_init(). */
static const uint8_t tlsr8258_wakeup_analog[][2] = {
	{0x82u, 0x64u},
	{0x34u, 0x80u},
	{0x0bu, 0x38u},
	{0x8cu, 0x02u},
	{0x02u, 0xa2u},
};

static void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
	TLSR8258_REG_ANA_CTRL = 0u;
}

void soc_prep_hook(void)
{
	/*
	 * Register part of the vendor cpu_wakeup_init(): release the
	 * peripheral resets and enable their clocks, then stop the watchdog.
	 */
	TLSR8258_REG_RST_CLK0 = 0xff000000u;
	/* clk_en1 and clk_en2 only; a word store would also write reg_clk_sel (0x66). */
	TLSR8258_REG8(0x00800064u) = 0xffu;
	TLSR8258_REG8(0x00800065u) = 0xffu;

	TLSR8258_REG_TMR_CTRL &= ~TLSR8258_FLD_TMR_WD_EN;
}

/* True once the system tick, which counts the crystal, advances. */
static bool tlsr8258_xtal_ready(void)
{
	uint32_t start = TLSR8258_REG_SYSTEM_TICK;

	for (uint32_t i = 0u; i < TLSR8258_XTAL_READY_SPINS; i++) {
		if ((TLSR8258_REG_SYSTEM_TICK - start) >=
		    TLSR8258_XTAL_READY_US * TLSR8258_TICKS_PER_US) {
			return true;
		}
	}

	return false;
}

/* The flash is fetched through the cache while the clock changes: switch from SRAM. */
static void __attribute__((noinline, section(".ram_code"))) tlsr8258_sys_clk_select(void)
{
	TLSR8258_REG_CLK_SEL = TLSR8258_SYS_CLK_SEL;
}

void soc_early_init_hook(void)
{
	/*
	 * Analog part of cpu_wakeup_init(): DCDC, LDO and clock trim. It has
	 * to follow the .data copy, as in the vendor startup: flash reads
	 * issued while these settle can return corrupted data.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(tlsr8258_wakeup_analog); i++) {
		tlsr8258_analog_write(tlsr8258_wakeup_analog[i][0],
				      tlsr8258_wakeup_analog[i][1]);
	}

	/*
	 * Select the system clock before any device initialises: the UART, SPI
	 * and PWM dividers follow the sys_clk node. The system tick has to run
	 * to time the crystal; the timer driver starts it again later.
	 */
	TLSR8258_REG_SYSTEM_TICK_MODE = 0u;
	TLSR8258_REG_SYSTEM_TICK_MODE = TLSR8258_FLD_SYSTEM_TICK_16M;
	TLSR8258_REG_SYSTEM_TICK_CTRL = TLSR8258_FLD_SYSTEM_TICK_START;

	if (TLSR8258_REG_CLK_SEL != TLSR8258_SYS_CLK_SEL && tlsr8258_xtal_ready()) {
		if (TLSR8258_SYS_CLK_HZ == 48000000) {
			/* The flash needs a higher supply before the clock goes up. */
			tlsr8258_analog_write(TLSR8258_AREG_FLASH_VOLTAGE,
					      TLSR8258_FLASH_VOLTAGE_48M);
		}
		tlsr8258_sys_clk_select();
	}
}
