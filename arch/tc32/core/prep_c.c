/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Full C support initialization
 *
 * Entered from reset.S in SVC mode on the interrupt stack, executing from
 * flash, with every interrupt masked. .bss and .data are not valid yet.
 */

#include <kernel_internal.h>
#include <zephyr/arch/cache.h>
#include <zephyr/arch/common/init.h>
#include <zephyr/arch/common/xip.h>
#include <zephyr/platform/hooks.h>

FUNC_NORETURN void z_prep_c(void)
{
	soc_prep_hook();

	arch_bss_zero();
	arch_data_copy();

#if CONFIG_ARCH_CACHE
	arch_cache_init();
#endif

	z_cstart();
	CODE_UNREACHABLE;
}
