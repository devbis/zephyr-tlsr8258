/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "soc.h"

/*
 * The power off of hal_pwrmgr_poweroff() of the PHY62XX SDK 3.1.1: the RAM is not retained, the
 * chip wakes on a level change of a pad and starts from the boot ROM, which reads the cause of the
 * start in AON->SLEEP_R[0]: 2 is the power off, and the ROM then skips the window of its loader. The
 * pads that wake it are those of wakeup.c.
 */
#define AON_PMCTL2_0 0x4000f01cU
#define AON_SLEEP_R0 0x4000f0c0U
#define AON_POWER_OFF 0x4000f000U

#define RESET_CAUSE_OFF_MODE 2U
#define POWER_OFF_KEY 0x5a5aa5a5U

void z_sys_poweroff(void)
{
	(void)irq_lock();

	/* Disable the software control of the power */
	sys_write32(sys_read32(AON_PMCTL2_0) & ~BIT(6), AON_PMCTL2_0);

	phy62x2_wakeup_arm(false);

	/* The reset cause for the boot ROM, then the key that powers the chip off */
	sys_write32(RESET_CAUSE_OFF_MODE, AON_SLEEP_R0);
	sys_write32(POWER_OFF_KEY, AON_POWER_OFF);

	for (;;) {
	}
}
