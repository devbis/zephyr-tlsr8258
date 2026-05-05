/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel_internal.h>
#include <zephyr/arch/cache.h>
#include <zephyr/arch/common/init.h>
#include <zephyr/arch/common/xip.h>
#include <zephyr/platform/hooks.h>

void z_prep_c(void)
{
	void (*volatile cstart)(void) = z_cstart;

	if (IS_ENABLED(CONFIG_ARCH_CACHE)) {
		arch_cache_init();
	}

	soc_prep_hook();
	arch_bss_zero();
	arch_data_copy();
	cstart();
	for (;;) {
		__asm__ volatile("tmov r8, r8");
	}
}
