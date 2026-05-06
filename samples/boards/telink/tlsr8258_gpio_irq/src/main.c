/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <tlsr825x/irq.h>

#define REG8(addr)  ((volatile uint8_t *)(addr))
#define REG32(addr) ((volatile uint32_t *)(addr))

#define GPIO_PA0 BIT(0)

#define GPIO_PA_IN           REG8(0x00800580u)
#define GPIO_PA_IE           REG8(0x00800581u)
#define GPIO_PA_OEN          REG8(0x00800582u)
#define GPIO_PA_OUT          REG8(0x00800583u)
#define GPIO_PA_POL          REG8(0x00800584u)
#define GPIO_PA_FUNC         REG8(0x00800586u)
#define GPIO_PA_IRQ_WAKEUP   REG8(0x00800587u)
#define GPIO_WAKEUP_IRQ      REG8(0x008005b5u)
#define GPIO_IRQ_RISC0_PA    REG8(0x008005b8u)
#define GPIO_IRQ_RISC1_PA    REG8(0x008005c0u)

#define FLD_GPIO_CORE_INTERRUPT_EN BIT(3)

volatile uint32_t tlsr_gpio_irq_count;
volatile uint32_t tlsr_gpio_irq_marker;
volatile uint32_t tlsr_gpio_irq_seen_mask;

extern void tc32_delay_cycles(uint32_t cycles);

static FUNC_NORETURN void park_with_marker(uint32_t marker)
{
	tlsr_gpio_irq_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

static void tiny_delay(void)
{
	tc32_delay_cycles(2000u);
}

static void clear_gpio_source(unsigned int irq)
{
	*TLSR8258_REG_IRQ_SRC = BIT(irq);
	tlsr_gpio_irq_seen_mask |= BIT(irq);
	tlsr_gpio_irq_count++;
}

static void gpio_irq_isr(const void *unused)
{
	ARG_UNUSED(unused);
	clear_gpio_source(TLSR8258_IRQ_GPIO);
}

static void gpio_risc0_irq_isr(const void *unused)
{
	ARG_UNUSED(unused);
	clear_gpio_source(TLSR8258_IRQ_GPIO_RISC0);
}

static void gpio_risc1_irq_isr(const void *unused)
{
	ARG_UNUSED(unused);
	clear_gpio_source(TLSR8258_IRQ_GPIO_RISC1);
}

static void gpio_pa0_prepare(void)
{
	unsigned int key = irq_lock();

	irq_disable(TLSR8258_IRQ_GPIO);
	irq_disable(TLSR8258_IRQ_GPIO_RISC0);
	irq_disable(TLSR8258_IRQ_GPIO_RISC1);

	*GPIO_PA_IRQ_WAKEUP &= (uint8_t)~GPIO_PA0;
	*GPIO_IRQ_RISC0_PA &= (uint8_t)~GPIO_PA0;
	*GPIO_IRQ_RISC1_PA &= (uint8_t)~GPIO_PA0;

	*GPIO_PA_FUNC |= GPIO_PA0;
	*GPIO_PA_IE |= GPIO_PA0;
	*GPIO_PA_OEN &= (uint8_t)~GPIO_PA0;
	*GPIO_PA_OUT &= (uint8_t)~GPIO_PA0;

	/* Rising-edge mode. Vendor sequence clears source after polarity setup. */
	*GPIO_PA_POL &= (uint8_t)~GPIO_PA0;
	*TLSR8258_REG_IRQ_SRC = BIT(TLSR8258_IRQ_GPIO) |
				 BIT(TLSR8258_IRQ_GPIO_RISC0) |
				 BIT(TLSR8258_IRQ_GPIO_RISC1);

	irq_unlock(key);
	tiny_delay();
}

static void trigger_pa0_rising_edge(void)
{
	*GPIO_PA_OUT &= (uint8_t)~GPIO_PA0;
	tiny_delay();
	*GPIO_PA_OUT |= GPIO_PA0;
	tiny_delay();
	*GPIO_PA_OUT &= (uint8_t)~GPIO_PA0;
	tiny_delay();
}

static void enable_gpio_irq(unsigned int irq, volatile uint8_t *pin_irq_enable)
{
	unsigned int key = irq_lock();

	tlsr_gpio_irq_seen_mask = 0u;
	*TLSR8258_REG_IRQ_SRC = BIT(irq);
	*pin_irq_enable |= GPIO_PA0;
	irq_enable(irq);

	irq_unlock(key);
}

static void disable_gpio_irq(unsigned int irq, volatile uint8_t *pin_irq_enable)
{
	unsigned int key = irq_lock();

	irq_disable(irq);
	*pin_irq_enable &= (uint8_t)~GPIO_PA0;
	*TLSR8258_REG_IRQ_SRC = BIT(irq);

	irq_unlock(key);
}

static bool wait_for_irq(unsigned int irq)
{
	uint32_t before = tlsr_gpio_irq_count;

	trigger_pa0_rising_edge();

	for (unsigned int i = 0; i < 2000u; i++) {
		if (tlsr_gpio_irq_count != before) {
			return (tlsr_gpio_irq_seen_mask & BIT(irq)) != 0u;
		}
		compiler_barrier();
	}

	return false;
}

static bool test_irq(unsigned int irq, volatile uint8_t *pin_irq_enable)
{
	bool ok;

	enable_gpio_irq(irq, pin_irq_enable);
	ok = wait_for_irq(irq);
	disable_gpio_irq(irq, pin_irq_enable);

	return ok;
}

int main(void)
{
	tlsr_gpio_irq_marker = 0x82582000u;

	IRQ_CONNECT(TLSR8258_IRQ_GPIO, 0, gpio_irq_isr, NULL, 0);
	IRQ_CONNECT(TLSR8258_IRQ_GPIO_RISC0, 0, gpio_risc0_irq_isr, NULL, 0);
	IRQ_CONNECT(TLSR8258_IRQ_GPIO_RISC1, 0, gpio_risc1_irq_isr, NULL, 0);

	gpio_pa0_prepare();

	*GPIO_WAKEUP_IRQ |= FLD_GPIO_CORE_INTERRUPT_EN;
	arch_irq_unlock(1u);

	if (!test_irq(TLSR8258_IRQ_GPIO, GPIO_PA_IRQ_WAKEUP)) {
		park_with_marker(0x8258e201u);
	}

	if (!test_irq(TLSR8258_IRQ_GPIO_RISC0, GPIO_IRQ_RISC0_PA)) {
		park_with_marker(0x8258e202u);
	}

	if (!test_irq(TLSR8258_IRQ_GPIO_RISC1, GPIO_IRQ_RISC1_PA)) {
		park_with_marker(0x8258e203u);
	}

	park_with_marker(0x82580000u);
}
