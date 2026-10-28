/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/platform/hooks.h>

extern const uintptr_t _vector_table[];
extern const uintptr_t _irq_vector_table[];

/* PHY62XX SDK 3.1.1 ROM clock entry points. */
#define ROM_CLK_INIT     ((int (*)(uint32_t))0x0000a5edU)
#define ROM_CLK_GET_PCLK ((uint32_t (*)(void))0x0000a5d1U)
#define SYS_CLK_XTAL_16M 2U

void soc_prep_hook(void)
{
	volatile uintptr_t *const jump = (volatile uintptr_t *)0x1fff0000U;
	volatile uint32_t *const reset = (volatile uint32_t *)0x40000000U;
	volatile uint32_t *const clock = (volatile uint32_t *)0x40000008U;
	volatile uint32_t *const mux_enable = (volatile uint32_t *)0x4000380cU;
	volatile uint32_t *const mux_select = (volatile uint32_t *)0x4000381cU;
	volatile uint32_t *const pull0 = (volatile uint32_t *)0x4000f008U;
	volatile uint32_t *const pull1 = (volatile uint32_t *)0x4000f00cU;

	jump[219] = _vector_table[2];
	jump[220] = _vector_table[3];
	jump[221] = _vector_table[11];
	jump[222] = _vector_table[14];
	jump[223] = _vector_table[15];
	for (uint32_t i = 0U; i < 32U; i++) {
		jump[224U + i] = _irq_vector_table[i];
	}

	/* Select the 16 MHz crystal and require the UART clock to match. */
	*(volatile uint8_t *)0x1fff0a4aU = SYS_CLK_XTAL_16M;
	if (ROM_CLK_INIT(SYS_CLK_XTAL_16M) != 0 || ROM_CLK_GET_PCLK() != 16000000U) {
		for (;;) {
			__asm__ volatile("wfi");
		}
	}

	/* UART0: P9 is TX and P10 is RX. */
	*clock |= (1U << 7) | (1U << 8);
	*reset &= ~(1U << 8);
	*reset |= 1U << 8;
	*pull0 = (*pull0 & ~(3U << 28)) | (2U << 28);
	*pull1 = (*pull1 & ~(3U << 1)) | (2U << 1);
	*mux_select = (*mux_select & ~0x003f3f00U) | 0x00050400U;
	*mux_enable |= (1U << 5) | (1U << 6);

#ifdef CONFIG_COUNTER_SNPS_DW
	/* Timers 5 and 6 share a reset and have a 4 MHz clock. */
	*clock |= (1U << 21) | (1U << 22);
	*reset &= ~(1U << 5);
	*reset |= 1U << 5;
#endif
}
