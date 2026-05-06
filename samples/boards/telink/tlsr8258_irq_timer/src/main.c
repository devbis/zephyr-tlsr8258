/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <tlsr825x/irq.h>
#include <stdint.h>

volatile uint32_t tlsr_irq_count;
volatile uint32_t tlsr_irq_marker;

extern void tc32_delay_cycles(uint32_t cycles);

static void wait_ms_by_cycles(uint32_t delay_ms)
{
	tc32_delay_cycles(k_ms_to_cyc_ceil32(delay_ms));
}

void tlsr8258_stimer_debug_tick(void)
{
	tlsr_irq_count++;
}

static FUNC_NORETURN void park_with_marker(uint32_t marker)
{
	tlsr_irq_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

int main(void)
{
	uint32_t before;
	uint32_t after_disable;

	tlsr_irq_marker = 0x82580001u;
	before = tlsr_irq_count;

	k_sleep(K_MSEC(100));
	if (tlsr_irq_count == before) {
		park_with_marker(0x8258e001u);
	}

	irq_disable(TLSR8258_IRQ_SYSTEM_TIMER);
	wait_ms_by_cycles(2u);
	after_disable = tlsr_irq_count;
	wait_ms_by_cycles(50u);
	if (tlsr_irq_count != after_disable) {
		park_with_marker(0x8258e002u);
	}

	irq_enable(TLSR8258_IRQ_SYSTEM_TIMER);
	before = tlsr_irq_count;
	k_sleep(K_MSEC(100));
	if (tlsr_irq_count == before) {
		park_with_marker(0x8258e003u);
	}

	park_with_marker(0x82580000u);
}
