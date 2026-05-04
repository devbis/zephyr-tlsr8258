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

static void wait_ms_by_cycles(uint32_t delay_ms)
{
	uint32_t start = k_cycle_get_32();
	uint32_t wait_cycles = k_ms_to_cyc_ceil32(delay_ms);

	while ((k_cycle_get_32() - start) < wait_cycles) {
	}
}

void tlsr8258_stimer_debug_tick(void)
{
	tlsr_irq_count++;
}

int main(void)
{
	uint32_t before;
	uint32_t after_disable;

	tlsr_irq_marker = 0x82580001u;
	before = tlsr_irq_count;

	k_sleep(K_MSEC(100));
	if (tlsr_irq_count == before) {
		tlsr_irq_marker = 0x8258e001u;
		return 1;
	}

	irq_disable(TLSR8258_IRQ_SYSTEM_TIMER);
	wait_ms_by_cycles(2u);
	after_disable = tlsr_irq_count;
	wait_ms_by_cycles(50u);
	if (tlsr_irq_count != after_disable) {
		tlsr_irq_marker = 0x8258e002u;
		return 2;
	}

	irq_enable(TLSR8258_IRQ_SYSTEM_TIMER);
	before = tlsr_irq_count;
	k_sleep(K_MSEC(100));
	if (tlsr_irq_count == before) {
		tlsr_irq_marker = 0x8258e003u;
		return 3;
	}

	tlsr_irq_marker = 0x82580000u;
	return 0;
}
