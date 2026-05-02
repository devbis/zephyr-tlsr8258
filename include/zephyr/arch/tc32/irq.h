/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_
#define ZEPHYR_INCLUDE_ARCH_TC32_IRQ_H_

#include <zephyr/sys/util.h>

#define TC32_NUM_IRQS 32

#ifndef _ASMLANGUAGE

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/irq.h>
#include <zephyr/sw_isr_table.h>

#define TC32_REG_IRQ_MASK ((volatile uint32_t *)0x00800640u)
#define TC32_REG_IRQ_EN   ((volatile uint8_t *)0x00800643u)

static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	uint8_t key = *TC32_REG_IRQ_EN;

	*TC32_REG_IRQ_EN = 0;
	return key & 1u;
}

static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
	*TC32_REG_IRQ_EN = key ? 1u : 0u;
}

static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	return key != 0U;
}

static ALWAYS_INLINE bool arch_cpu_irqs_are_enabled(void)
{
	return (*TC32_REG_IRQ_EN & 1u) != 0u;
}

#define arch_irq_enable(irq) z_tc32_irq_enable(irq)
#define arch_irq_disable(irq) z_tc32_irq_disable(irq)
#define arch_irq_is_enabled(irq) z_tc32_irq_is_enabled(irq)

static ALWAYS_INLINE void z_tc32_irq_enable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();

	*TC32_REG_IRQ_MASK |= BIT(irq);
	arch_irq_unlock(key);
}

static ALWAYS_INLINE void z_tc32_irq_disable(unsigned int irq)
{
	unsigned int key = arch_irq_lock();

	*TC32_REG_IRQ_MASK &= ~BIT(irq);
	arch_irq_unlock(key);
}

static ALWAYS_INLINE int z_tc32_irq_is_enabled(unsigned int irq)
{
	return (*TC32_REG_IRQ_MASK & BIT(irq)) != 0u;
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
