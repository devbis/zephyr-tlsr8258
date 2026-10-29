/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/platform/hooks.h>
#include <zephyr/sys/util.h>

/* Register addresses from the vendor chip_826x/register.h. */
#define TLSR8269_REG_RST_CLK0 (*(volatile uint32_t *)0x00800060u)
#define TLSR8269_REG_RST_CLK1 (*(volatile uint32_t *)0x00800064u)

#define TLSR8269_REG_TMR_CTRL  (*(volatile uint32_t *)0x00800620u)
#define TLSR8269_FLD_TMR_WD_EN BIT(23)

#define TLSR8269_REG_ANA_ADDR (*(volatile uint8_t *)0x008000b8u)
#define TLSR8269_REG_ANA_DATA (*(volatile uint8_t *)0x008000b9u)
#define TLSR8269_REG_ANA_CTRL (*(volatile uint8_t *)0x008000bau)

#define TLSR8269_FLD_ANA_BUSY BIT(0)
#define TLSR8269_FLD_ANA_RW   BIT(5)
#define TLSR8269_FLD_ANA_CYC  BIT(7)

#define TLSR8269_REG_CLK_SEL (*(volatile uint8_t *)0x00800066u)

/* Analog register the vendor clock_init() writes before it selects a PLL clock. */
#define TLSR8269_AREG_PLL 0x88u
#define TLSR8269_PLL_ON   0x0fu

/*
 * The system clock is the frequency of the sys_clk devicetree node, and the
 * drivers divide it to get their rates, so what the SoC programs here has to
 * be what the node says. The vendor SDK offers 16 and 32 MHz from the PLL on
 * this SoC. The system timer driver counts the system tick as 16 MHz, and on
 * 826x the tick follows the system clock, so only 16 MHz is accepted.
 */
#define TLSR8269_SYS_CLK_HZ DT_PROP(DT_NODELABEL(sys_clk), clock_frequency)

#if TLSR8269_SYS_CLK_HZ == 16000000
#define TLSR8269_SYS_CLK_SEL 0x2cu
#else
#error "sys_clk must be 16 MHz"
#endif

static void tlsr8269_analog_write(uint8_t addr, uint8_t value)
{
	TLSR8269_REG_ANA_ADDR = addr;
	TLSR8269_REG_ANA_DATA = value;
	TLSR8269_REG_ANA_CTRL = TLSR8269_FLD_ANA_CYC | TLSR8269_FLD_ANA_RW;
	while ((TLSR8269_REG_ANA_CTRL & TLSR8269_FLD_ANA_BUSY) != 0u) {
	}
	TLSR8269_REG_ANA_CTRL = 0u;
}

/* The flash is fetched through the cache while the clock changes: switch from SRAM. */
static void __attribute__((noinline, section(".ram_code"))) tlsr8269_sys_clk_select(void)
{
	TLSR8269_REG_CLK_SEL = TLSR8269_SYS_CLK_SEL;
}

void soc_prep_hook(void)
{
	/*
	 * Register part of the vendor cpu_wakeup_init(): release the
	 * peripheral resets and enable their clocks, then stop the watchdog.
	 * The analog trim sequence (0xb8..0xba) is not ported: the 8258
	 * values in tlsr825x/soc.c are not known to match this chip.
	 */
	TLSR8269_REG_RST_CLK0 = 0xff000000u;
	TLSR8269_REG_RST_CLK1 = 0x0006ffffu;

	TLSR8269_REG_TMR_CTRL &= ~TLSR8269_FLD_TMR_WD_EN;
}

void soc_early_init_hook(void)
{
	/*
	 * The analog trim of the 825x series (tlsr825x/soc.c) is not ported.
	 * Its values are not known to match the 826x analog block. Only the
	 * system clock is set, as the vendor clock_init() sets 16 MHz.
	 */
	tlsr8269_analog_write(TLSR8269_AREG_PLL, TLSR8269_PLL_ON);
	tlsr8269_sys_clk_select();
}
