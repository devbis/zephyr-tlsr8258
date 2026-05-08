/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <kswap.h>
#include <tlsr825x/irq.h>

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

#define Z_TC32_BRANCH_TARGET(fn) ((uintptr_t)(fn))
#define TC32_IRQ_MAX_DRAIN (TLSR8258_NUM_IRQS * 2u)

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

	((void (*)(const void *))Z_TC32_BRANCH_TARGET(ite->isr))(ite->arg);

	if (IS_ENABLED(CONFIG_TRACING_ISR)) {
		sys_trace_isr_exit();
	}
}

static ALWAYS_INLINE bool irq_is_valid(unsigned int irq)
{
	return tlsr8258_irq_is_valid(irq);
}

static ALWAYS_INLINE bool irq_clear_is_arch_owned(unsigned int irq)
{
	return (tlsr8258_irq_bit(irq) & TLSR8258_IRQ_TIMER_MASK) != 0u;
}

static ALWAYS_INLINE void irq_clear_arch_owned(unsigned int irq)
{
	/* Vendor order for TMR0..TMR2: clear IRQSRC first, then TMR_STATUS. */
	*TLSR8258_REG_IRQ_SRC = BIT(irq);
	*TLSR8258_REG_TMR_STA = BIT(irq);
}

static ALWAYS_INLINE unsigned int pending_lsb_index(uint32_t pending)
{
	unsigned int irq = 0u;

	while ((pending & BIT(irq)) == 0u) {
		irq++;
	}

	return irq;
}

void z_tc32_handle_irqs(void)
{
	uint32_t pending;
	unsigned int drained = 0u;

	/*
	 * First-stage TC32 port policy: process all currently pending sources
	 * with global IRQ still disabled. Hardware nested IRQ entry is not
	 * enabled here; arch_is_in_isr() observes this counter for the whole
	 * pending-drain loop.
	 */
	_kernel.cpus[0].nested++;

	while ((pending = (*TLSR8258_REG_IRQ_SRC & *TLSR8258_REG_IRQ_MASK & TLSR8258_IRQ_VALID_MASK)) != 0u) {
		unsigned int irq = pending_lsb_index(pending);

		if (!irq_is_valid(irq)) {
			break;
		}

		if (drained++ >= TC32_IRQ_MAX_DRAIN) {
			_kernel.cpus[0].nested--;
			z_tc32_fatal_error(K_ERR_SPURIOUS_IRQ, NULL);
		}

		if (irq_clear_is_arch_owned(irq)) {
			irq_clear_arch_owned(irq);
		}

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
