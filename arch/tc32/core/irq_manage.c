/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/tc32/arch.h>
#include <zephyr/logging/log.h>
#include <zephyr/linker/section_tags.h>
#include <ksched.h>
#include <kswap.h>
#include <tlsr825x/irq.h>

LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);

#define TC32_IRQ_MAX_DRAIN (TLSR8258_NUM_IRQS * 2U)

volatile uint32_t z_tc32_irq_count;
volatile uint8_t z_tc32_irq_soft_masked;

/*
 * Set by z_tc32_handle_irqs() when it decides to switch, read by the exit
 * path in reset.S: the thread whose interrupt frame has to be relocated.
 */
struct k_thread *z_tc32_switch_from;

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

static ALWAYS_INLINE bool irq_is_valid(unsigned int irq)
{
	return tlsr8258_irq_is_valid(irq);
}

static ALWAYS_INLINE bool irq_clear_is_arch_owned(unsigned int irq)
{
	return (tlsr8258_irq_bit(irq) & TLSR8258_IRQ_TIMER_MASK) != 0U;
}

static ALWAYS_INLINE void irq_clear_arch_owned(unsigned int irq)
{
	/* Vendor order for TMR0..TMR2: clear IRQSRC first, then TMR_STATUS. */
	tlsr8258_irq_clear_parent(irq);
	*TLSR8258_REG_TMR_STA = BIT(irq);
}

static ALWAYS_INLINE unsigned int pending_lsb_index(uint32_t pending)
{
	unsigned int irq = 0U;

	while ((pending & BIT(irq)) == 0U) {
		irq++;
	}

	return irq;
}

void *TC32_BOOT_RAM_MIRROR_CODE z_tc32_handle_irqs(void)
{
	uint32_t pending;
	unsigned int drained = 0U;
	/*
	 * A handler that reopens the gate and then takes and releases a lock
	 * would clear the flag of the section this interrupt arrived in.
	 */
	uint8_t soft_masked = z_tc32_irq_soft_masked;

	/*
	 * Handlers run with the CPU I bit set, there is no nesting. All
	 * pending sources are drained in one entry, lowest line first, except
	 * TLSR8258_IRQ_PREFERRED: when it is pending it runs first and ends
	 * the entry, and the other sources are taken on the next one.
	 */
	_kernel.cpus[0].nested++;
	z_tc32_irq_count++;

	while ((pending = (*TLSR8258_REG_IRQ_SRC & tlsr8258_irq_mask_read() &
			  TLSR8258_IRQ_VALID_MASK)) != 0U) {
		unsigned int irq;
		bool preferred = (pending & BIT(TLSR8258_IRQ_PREFERRED)) != 0U;

		if (preferred) {
			irq = TLSR8258_IRQ_PREFERRED;
		} else {
			irq = pending_lsb_index(pending);
		}

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

		if (preferred) {
			break;
		}
	}

	_kernel.cpus[0].nested--;
	z_tc32_irq_soft_masked = soft_masked;

	if (IS_ENABLED(CONFIG_STACK_SENTINEL)) {
		z_check_stack_sentinel();
	}

	/*
	 * An interrupt can still be taken just after arch_irq_lock() closed
	 * the gate. The interrupted code is then inside a critical section,
	 * possibly do_swap() with _current already switched: switching away
	 * here would park the wrong thread.
	 */
	if (!IS_ENABLED(CONFIG_MULTITHREADING) || _kernel.cpus[0].nested != 0U ||
	    soft_masked != 0U) {
		return NULL;
	}

	/*
	 * Handlers routinely ready a higher priority thread, and
	 * k_thread_abort() on the running thread from an ISR only marks it
	 * dead and leaves the arch to stop running it. Ask the scheduler who
	 * should run; the exit path in reset.S does the stack surgery, since
	 * the interrupt frame sits on the banked IRQ stack and has to move to
	 * the outgoing thread before anyone else can use that stack.
	 */
	struct k_thread *interrupted = _current;
	void *next = z_get_next_switch_handle(interrupted);

	if (next == (void *)interrupted) {
		return NULL;
	}

	z_tc32_switch_from = interrupted;

	return next;
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
