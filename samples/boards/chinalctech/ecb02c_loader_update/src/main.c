/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "loader_update.h"

int main(void)
{
	const int rc = loader_update();

	printk("ECB02C loader update: loader_update() = %d\n", rc);
	if (rc == 1) {
		printk("the new loader is in the flash, rebooting\n");
		k_msleep(200);
		sys_reboot(SYS_REBOOT_COLD);
	}

	return 0;
}
