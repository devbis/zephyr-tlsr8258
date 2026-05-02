/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_ARCH_TC32_INCLUDE_KERNEL_ARCH_FUNC_H_
#define ZEPHYR_ARCH_TC32_INCLUDE_KERNEL_ARCH_FUNC_H_

#include <kernel_arch_data.h>
#include <zephyr/platform/hooks.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASMLANGUAGE

static ALWAYS_INLINE void arch_kernel_init(void)
{
	soc_per_core_init_hook();
}

static ALWAYS_INLINE void arch_switch(void *switch_to, void **switched_from)
{
	extern void z_tc32_switch(struct k_thread *new, struct k_thread *old);
	struct k_thread *new_thread = switch_to;
	struct k_thread *old_thread = CONTAINER_OF(switched_from, struct k_thread,
						   switch_handle);

	z_tc32_switch(new_thread, old_thread);
}

FUNC_NORETURN void z_tc32_fatal_error(unsigned int reason, const struct arch_esf *esf);

static inline bool arch_is_in_isr(void)
{
	return _kernel.cpus[0].nested != 0U;
}

#endif /* _ASMLANGUAGE */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_ARCH_TC32_INCLUDE_KERNEL_ARCH_FUNC_H_ */
