/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <kswap.h>

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

#define TC32_REG_IRQ_SRC ((volatile uint32_t *)0x00800648u)

FUNC_NORETURN void z_irq_spurious(const void *unused)
{
	ARG_UNUSED(unused);

	LOG_ERR("Spurious interrupt detected");
	z_tc32_fatal_error(K_ERR_SPURIOUS_IRQ, NULL);
}

static ALWAYS_INLINE void enter_irq(unsigned int irq)
{
	const struct _isr_table_entry *ite = &_sw_isr_table[irq];

	if (IS_ENABLED(CONFIG_TRACING_ISR)) {
		sys_trace_isr_enter();
	}

	ite->isr(ite->arg);

	if (IS_ENABLED(CONFIG_TRACING_ISR)) {
		sys_trace_isr_exit();
	}
}

void z_tc32_handle_irqs(void)
{
	uint32_t pending;

	_kernel.cpus[0].nested++;

	while ((pending = (*TC32_REG_IRQ_SRC & *TC32_REG_IRQ_MASK)) != 0u) {
		unsigned int irq = find_lsb_set(pending) - 1u;

		*TC32_REG_IRQ_SRC = BIT(irq);
		enter_irq(irq);
	}

	_kernel.cpus[0].nested--;

	if (IS_ENABLED(CONFIG_STACK_SENTINEL)) {
		z_check_stack_sentinel();
	}
}

#ifdef CONFIG_DYNAMIC_INTERRUPTS
int arch_irq_connect_dynamic(unsigned int irq, unsigned int priority,
			     void (*routine)(const void *parameter),
			     const void *parameter, uint32_t flags)
{
	ARG_UNUSED(priority);
	ARG_UNUSED(flags);

	z_isr_install(irq, routine, parameter);
	return irq;
}
#endif /* CONFIG_DYNAMIC_INTERRUPTS */
