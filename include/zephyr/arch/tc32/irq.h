/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_

#include <zephyr/sys/util.h>

#ifndef _ASMLANGUAGE

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/irq.h>
#include <zephyr/sw_isr_table.h>
#include <tlsr825x/irq.h>

#define TC32_NUM_IRQS TLSR8258_NUM_IRQS

/* Bumped on every interrupt entry, see arch_cpu_idle(). */
extern volatile uint32_t z_tc32_irq_count;

/*
 * Set while an irq_lock() section holds reg_irq_en closed. An interrupt can
 * still be taken right after the closing store; its exit path then closes
 * the gate again and does not switch threads. The interrupt entry saves and
 * restores it, so locks taken inside a handler do not change it.
 */
extern volatile uint8_t z_tc32_irq_soft_masked;

static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	uint8_t key = *TLSR8258_REG_IRQ_EN;

	/* Flag the section before the gate closes, see above. */
	if ((key & 1U) != 0U) {
		z_tc32_irq_soft_masked = 1U;
	}
	__asm__ volatile("" ::: "memory");
	*TLSR8258_REG_IRQ_EN = 0U;

	return key;
}

static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
	if ((key & 1U) != 0U) {
		z_tc32_irq_soft_masked = 0U;
	}
	__asm__ volatile("" ::: "memory");
	*TLSR8258_REG_IRQ_EN = (uint8_t)key;
}

static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	return (key & 1U) != 0U;
}

static ALWAYS_INLINE bool arch_cpu_irqs_are_enabled(void)
{
	return (*TLSR8258_REG_IRQ_EN & 1U) != 0U;
}

#define arch_irq_enable(irq) z_tc32_irq_enable(irq)
#define arch_irq_disable(irq) z_tc32_irq_disable(irq)
#define arch_irq_is_enabled(irq) z_tc32_irq_is_enabled(irq)

static ALWAYS_INLINE void z_tc32_irq_enable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();
	uint32_t bit = tlsr8258_irq_bit(irq);

	if ((bit & TLSR8258_IRQ_VALID_MASK) != 0u) {
		tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() | bit);
	}
	arch_irq_unlock(key);
}

static ALWAYS_INLINE void z_tc32_irq_disable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();
	uint32_t bit = tlsr8258_irq_bit(irq);

	if (bit != 0u) {
		tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() & ~bit);
	}
	arch_irq_unlock(key);
}

static ALWAYS_INLINE int z_tc32_irq_is_enabled(unsigned int irq)
{
	uint32_t bit = tlsr8258_irq_bit(irq);

	return (bit & TLSR8258_IRQ_VALID_MASK & tlsr8258_irq_mask_read()) != 0U;
}

#define ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
	{ Z_ISR_DECLARE(irq_p, 0, isr_p, isr_param_p); }

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_ */
