/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#include <tlsr82xx/irq.h>
#include "dma_irq.h"

#define TLSR825X_REG_DMA_IRQ_STATUS (*(volatile uint8_t *)0x00800c26u)

#define TLSR825X_DMA_IRQ_MAX_HANDLERS 4u

struct tlsr825x_dma_irq_entry {
	tlsr825x_dma_irq_handler_t handler;
	const void *arg;
};

static struct tlsr825x_dma_irq_entry tlsr825x_dma_irq_entries[TLSR825X_DMA_IRQ_MAX_HANDLERS];
static uint8_t tlsr825x_dma_irq_masks[TLSR825X_DMA_IRQ_MAX_HANDLERS];
static uint8_t tlsr825x_dma_irq_count;

/* In SRAM like the radio handler it can call: no instruction fetch from flash. */
__attribute__((section(".ram_code"))) static void tlsr825x_dma_irq_isr(const void *unused)
{
	uint32_t selected = tlsr825x_dma_irq_select(tlsr825x_dma_irq_masks, tlsr825x_dma_irq_count,
						    TLSR825X_REG_DMA_IRQ_STATUS);

	ARG_UNUSED(unused);

	for (uint8_t i = 0u; i < tlsr825x_dma_irq_count; i++) {
		if ((selected & BIT(i)) != 0u) {
			tlsr825x_dma_irq_entries[i].handler(tlsr825x_dma_irq_entries[i].arg);
		}
	}
}

int tlsr825x_dma_irq_register(uint8_t chan_mask, tlsr825x_dma_irq_handler_t handler,
			      const void *arg)
{
	unsigned int key;
	uint8_t i;

	if (chan_mask == 0u || handler == NULL) {
		return -EINVAL;
	}

	key = irq_lock();
	i = tlsr825x_dma_irq_count;
	if (i >= TLSR825X_DMA_IRQ_MAX_HANDLERS) {
		irq_unlock(key);
		return -ENOMEM;
	}
	tlsr825x_dma_irq_entries[i].handler = handler;
	tlsr825x_dma_irq_entries[i].arg = arg;
	tlsr825x_dma_irq_masks[i] = chan_mask;
	tlsr825x_dma_irq_count = i + 1u;
	irq_unlock(key);

	return 0;
}

static int tlsr825x_dma_irq_init(void)
{
	IRQ_CONNECT(TLSR82XX_IRQ_DMA, 0, tlsr825x_dma_irq_isr, NULL, 0);

	return 0;
}

SYS_INIT(tlsr825x_dma_irq_init, PRE_KERNEL_1, 0);
