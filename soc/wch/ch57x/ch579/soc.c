/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/sys_io.h>

#define CH579_SYS_BASE         0x40001000UL
#define CH579_CLK_SYS_CFG      (CH579_SYS_BASE + 0x08UL) /* 16 bit, safe access */
#define CH579_SAFE_ACCESS_SIG  (CH579_SYS_BASE + 0x40UL) /* 8 bit */
#define CH579_CFG_FLASH        (CH579_SYS_BASE + 0x4aUL) /* 8 bit, safe access */

#define CH579_SAFE_SIG1        0x57U
#define CH579_SAFE_SIG2        0xa8U
#define CH579_CFG_FLASH_X_MASK 0x0fU

/* Fsys taken directly from the internal 32 MHz oscillator */
#define CH579_CLK_SYS_32M_HSI  ((2U << 6) | 0x08U)

static void ch579_safe_write8(mm_reg_t reg, uint8_t val)
{
	sys_write8(CH579_SAFE_SIG1, CH579_SAFE_ACCESS_SIG);
	sys_write8(CH579_SAFE_SIG2, CH579_SAFE_ACCESS_SIG);
	sys_write8(val, reg);
	sys_write8(0U, CH579_SAFE_ACCESS_SIG);
}

static void ch579_safe_write16(mm_reg_t reg, uint16_t val)
{
	sys_write8(CH579_SAFE_SIG1, CH579_SAFE_ACCESS_SIG);
	sys_write8(CH579_SAFE_SIG2, CH579_SAFE_ACCESS_SIG);
	sys_write16(val, reg);
	sys_write8(0U, CH579_SAFE_ACCESS_SIG);
}

void soc_early_init_hook(void)
{
	uint8_t flash_cfg = sys_read8(CH579_CFG_FLASH);
	uint8_t x = flash_cfg & CH579_CFG_FLASH_X_MASK;

	/* Same flash timing fix-up as the vendor SystemInit() */
	if (x >= 8U && x <= 13U) {
		x = MIN(2U + (x - 8U), 6U);
		ch579_safe_write8(CH579_CFG_FLASH,
				  (flash_cfg & ~CH579_CFG_FLASH_X_MASK) | x);
	}

	ch579_safe_write16(CH579_CLK_SYS_CFG, CH579_CLK_SYS_32M_HSI);
}
