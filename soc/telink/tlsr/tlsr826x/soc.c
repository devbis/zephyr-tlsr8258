/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/platform/hooks.h>
#include <zephyr/sys/util.h>

/* Register addresses from the vendor chip_826x/register.h. */
#define TLSR8269_REG_RST_CLK0 (*(volatile uint32_t *)0x00800060u)
#define TLSR8269_REG_RST_CLK1 (*(volatile uint32_t *)0x00800064u)

#define TLSR8269_REG_TMR_CTRL  (*(volatile uint32_t *)0x00800620u)
#define TLSR8269_FLD_TMR_WD_EN BIT(23)

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
	 * Its values are not known to match the 826x analog block.
	 */
}
