/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

void __weak sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	for (;;) {
		arch_nop();
	}
}
