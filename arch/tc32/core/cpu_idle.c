/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq.h>
#include <zephyr/tracing/tracing.h>

#include <tlsr825x/irq.h>

/*
 * Both entry points return only once an interrupt has been taken, which is
 * what callers of k_cpu_idle() rely on.
 *
 * The core spins instead of stopping. It can stall on reg_pwdn_ctrl bit 7,
 * but only resumes from the sources armed in reg_mcu_wakeup_mask (timers,
 * GPIO, comparator); the radio is not among them, so a stalled core would
 * miss radio interrupts.
 */
static ALWAYS_INLINE void tc32_wait_for_interrupt(unsigned int key)
{
	uint32_t taken = z_tc32_irq_count;

	arch_irq_unlock(key);

	while (z_tc32_irq_count == taken) {
	}
}

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_IDLE
void arch_cpu_idle(void)
{
	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle();
	}

	tc32_wait_for_interrupt(1);

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle_exit();
	}
}
#endif

#ifndef CONFIG_ARCH_HAS_CUSTOM_CPU_ATOMIC_IDLE
void arch_cpu_atomic_idle(unsigned int key)
{
	/*
	 * With the caller's gate closed no interrupt can be taken, so there
	 * is nothing to wait for. The gate stays closed, as after a wakeup.
	 */
	if (!arch_irq_unlocked(key)) {
		return;
	}

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle();
	}

	tc32_wait_for_interrupt(key);

	if (IS_ENABLED(CONFIG_TRACING)) {
		sys_trace_idle_exit();
	}
}
#endif
