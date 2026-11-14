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
#include <zephyr/storage/flash_map.h>
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

/*
 * Standby: the application sets a magic value in the last backup register to sleep and clears it
 * when it runs again. Both the reset that precedes the sleep and the wakeup from it go through
 * here, so the loader would spend its update window and the image check on each of them. Start
 * the application at once instead; the image was checked when it was started the first time. A
 * hung application is reset by its watchdog with the value already cleared, which opens the
 * window as usual.
 */
#ifdef CONFIG_MCUBOOT
#define WB_STANDBY_MAGIC_SLEEP	0x53424f53U
#define WB_STANDBY_MAGIC_WAKE	0x53424f4fU
#define WB_IMAGE_MAGIC		0x96f3b83dU
#define WB_SLOT0		\
	(CONFIG_FLASH_BASE_ADDRESS + FIXED_PARTITION_OFFSET(slot0_partition))
#define WB_SRAM_START		0x20000000U
#define WB_SRAM_END		0x20040000U

static int wb_standby_fast_boot(void)
{
	const uint32_t *header = (const uint32_t *)WB_SLOT0;
	const uint32_t *vt;
	uint32_t hdr_size;

	if ((RTC->BKP19R != WB_STANDBY_MAGIC_SLEEP) &&
	    (RTC->BKP19R != WB_STANDBY_MAGIC_WAKE)) {
		return 0;
	}

	hdr_size = *(const uint16_t *)(WB_SLOT0 + 8U);
	vt = (const uint32_t *)(WB_SLOT0 + hdr_size);
	if ((header[0] != WB_IMAGE_MAGIC) || (vt[0] < WB_SRAM_START) || (vt[0] > WB_SRAM_END) ||
	    ((vt[1] & 1U) == 0U) || (vt[1] < WB_SLOT0) ||
	    (vt[1] >= (WB_SLOT0 + FIXED_PARTITION_SIZE(slot0_partition)))) {
		return 0;
	}

	__disable_irq();
	SCB->VTOR = (uint32_t)vt;
	__set_MSP(vt[0]);
	((void (*)(void))vt[1])();

	return 0;
}
SYS_INIT(wb_standby_fast_boot, PRE_KERNEL_1, 1);
#endif /* CONFIG_MCUBOOT */
