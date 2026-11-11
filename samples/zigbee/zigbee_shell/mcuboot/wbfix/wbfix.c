/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * STM32WB: leaving the ROM DFU bootloader is a jump, not a reset, and it can leave sticky error
 * flags and CFGBSY set in FLASH_SR. The flash driver then fails every erase and write with -EIO
 * until the next reset, which breaks an image upload to MCUboot right after the first flashing.
 * Reset once, early, when that state is found: the reset clears it.
 *
 * OPTVERR is not part of the check: every reset sets it again, so resetting on it would loop.
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <soc.h>

/* OPERR, PROGERR, WRPERR, PGAERR, SIZERR, PGSERR */
#define WB_FLASH_SR_ERRORS 0x000000FAU

static int wb_flash_state_fix(void)
{
	if ((FLASH->SR & (WB_FLASH_SR_ERRORS | FLASH_SR_CFGBSY)) != 0U) {
		FLASH->SR = WB_FLASH_SR_ERRORS;
		NVIC_SystemReset();
	}

	return 0;
}
SYS_INIT(wb_flash_state_fix, PRE_KERNEL_1, 0);
