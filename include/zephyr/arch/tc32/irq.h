/*
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

static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	uint8_t key = *TLSR8258_REG_IRQ_EN;

	*TLSR8258_REG_IRQ_EN = 0;
	return key;
}

static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
	*TLSR8258_REG_IRQ_EN = (uint8_t)key;
}

static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	return (key & 1U) != 0U;
}

static ALWAYS_INLINE bool arch_cpu_irqs_are_enabled(void)
{
	return (*TLSR8258_REG_IRQ_EN & 1u) != 0u;
}

#define arch_irq_enable(irq) z_tc32_irq_enable(irq)
#define arch_irq_disable(irq) z_tc32_irq_disable(irq)
#define arch_irq_is_enabled(irq) z_tc32_irq_is_enabled(irq)

static ALWAYS_INLINE void z_tc32_irq_enable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();
	uint32_t bit = tlsr8258_irq_bit(irq);

	if ((bit & TLSR8258_IRQ_VALID_MASK) != 0u) {
		*TLSR8258_REG_IRQ_MASK |= bit;
	}
	arch_irq_unlock(key);
}

static ALWAYS_INLINE void z_tc32_irq_disable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();
	uint32_t bit = tlsr8258_irq_bit(irq);

	if (bit != 0u) {
		*TLSR8258_REG_IRQ_MASK &= ~bit;
	}
	arch_irq_unlock(key);
}

static ALWAYS_INLINE int z_tc32_irq_is_enabled(unsigned int irq)
{
	uint32_t bit = tlsr8258_irq_bit(irq);

	return (bit & TLSR8258_IRQ_VALID_MASK & *TLSR8258_REG_IRQ_MASK) != 0u;
}

#define ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
	{ Z_ISR_DECLARE(irq_p, 0, isr_p, isr_param_p); }

#define ARCH_IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p) \
	ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, NULL, flags_p)

#define ARCH_ISR_DIRECT_HEADER() do { } while (false)
#define ARCH_ISR_DIRECT_FOOTER(swap) do { ARG_UNUSED(swap); } while (false)
#define ARCH_ISR_DIRECT_DECLARE(name) \
	static inline int name##_body(void); \
	int name(void) \
	{ \
		return name##_body(); \
	} \
	static inline int name##_body(void)

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_ */
