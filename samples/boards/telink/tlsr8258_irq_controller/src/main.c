/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <tlsr825x/irq.h>

volatile uint32_t tlsr_irqctl_marker;
volatile uint32_t tlsr_irqctl_bad_irq;
volatile uint32_t tlsr_irqctl_mask_snapshot;

static FUNC_NORETURN void park(uint32_t marker)
{
	tlsr_irqctl_marker = marker;
	tlsr_irqctl_mask_snapshot = *TLSR8258_REG_IRQ_MASK & GENMASK(23, 0);

	for (;;) {
		compiler_barrier();
	}
}

int main(void)
{
	uint32_t saved_mask;
	unsigned int key;

	tlsr_irqctl_marker = 0x82584000u;
	key = irq_lock();
	saved_mask = *TLSR8258_REG_IRQ_MASK;
	*TLSR8258_REG_IRQ_MASK = 0u;

	for (unsigned int irq = 0u; irq < TLSR8258_NUM_IRQS + 2u; irq++) {
		bool valid = tlsr8258_irq_is_valid(irq);

		irq_enable(irq);
		if (irq_is_enabled(irq) != valid) {
			tlsr_irqctl_bad_irq = irq;
			*TLSR8258_REG_IRQ_MASK = saved_mask;
			irq_unlock(key);
			park(0x8258e401u);
		}

		irq_disable(irq);
		if (irq_is_enabled(irq)) {
			tlsr_irqctl_bad_irq = irq;
			*TLSR8258_REG_IRQ_MASK = saved_mask;
			irq_unlock(key);
			park(0x8258e402u);
		}

		if (tlsr8258_irq_is_edge(irq)) {
			tlsr8258_irq_clear_edge(irq);
		}
	}

	if ((*TLSR8258_REG_IRQ_MASK & TLSR8258_IRQ_RESERVED_MASK) != 0u) {
		*TLSR8258_REG_IRQ_MASK = saved_mask;
		irq_unlock(key);
		park(0x8258e403u);
	}

	*TLSR8258_REG_IRQ_MASK = saved_mask;
	irq_unlock(key);
	park(0x82580000u);
}
