/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/platform/hooks.h>
#include <zephyr/sys/util.h>

#define TLSR8258_REG_RST_CLK0 (*(volatile uint32_t *)0x00800060u)
#define TLSR8258_REG_RST_CLK1 (*(volatile uint32_t *)0x00800064u)

#define TLSR8258_REG_TMR_CTRL  (*(volatile uint32_t *)0x00800620u)
#define TLSR8258_FLD_TMR_WD_EN BIT(23)

#define TLSR8258_REG_ANA_ADDR (*(volatile uint8_t *)0x008000b8u)
#define TLSR8258_REG_ANA_DATA (*(volatile uint8_t *)0x008000b9u)
#define TLSR8258_REG_ANA_CTRL (*(volatile uint8_t *)0x008000bau)

#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

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
	TLSR8258_REG_RST_CLK1 = 0x0006ffffu;

	TLSR8258_REG_TMR_CTRL &= ~TLSR8258_FLD_TMR_WD_EN;
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
}
