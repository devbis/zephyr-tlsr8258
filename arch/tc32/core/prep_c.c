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
	uintptr_t cstart_addr = (uintptr_t)z_cstart & ~1u;
	void (*volatile cstart)(void) = (void (*)(void))cstart_addr;

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
