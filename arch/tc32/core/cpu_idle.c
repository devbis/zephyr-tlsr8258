/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/tracing/tracing.h>

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_IDLE
void arch_cpu_idle(void)
{
	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle();
	}

	arch_irq_unlock(1);

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle_exit();
	}
}
#endif

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_ATOMIC_IDLE
void arch_cpu_atomic_idle(unsigned int key)
{
	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle();
	}

	arch_irq_unlock(key);

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle_exit();
	}
}
#endif
