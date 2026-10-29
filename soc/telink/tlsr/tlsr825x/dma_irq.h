/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_TELINK_TLSR825X_DMA_IRQ_H_
#define SOC_TELINK_TLSR825X_DMA_IRQ_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handler of the DMA IRQ line for a set of DMA channels.
 *
 * It is called from the interrupt when the status register (0xc26) shows a
 * channel of its mask. It clears the status bits of its channels itself and
 * must not touch those of the others.
 */
typedef void (*tlsr825x_dma_irq_handler_t)(const void *arg);

/**
 * @brief Attach a handler to DMA channels on the shared DMA IRQ line.
 *
 * The line has one vector, so the drivers that use DMA (the UART and the
 * 802.15.4 radio) register here instead of connecting the line themselves.
 * The line is enabled and masked by the drivers as before.
 *
 * @param chan_mask DMA channels (bits of 0xc26) the handler serves
 * @param handler   handler to call
 * @param arg       argument passed to the handler
 *
 * @retval 0 on success
 * @retval -EINVAL @p chan_mask is 0 or @p handler is NULL
 * @retval -ENOMEM the table of handlers is full
 */
int tlsr825x_dma_irq_register(uint8_t chan_mask, tlsr825x_dma_irq_handler_t handler,
			      const void *arg);

/* Which handlers of a table a status value selects: bit i is set for entry i. */
static inline uint32_t tlsr825x_dma_irq_select(const uint8_t *masks, uint8_t count,
					       uint8_t status)
{
	uint32_t selected = 0u;

	for (uint8_t i = 0u; i < count; i++) {
		if ((masks[i] & status) != 0u) {
			selected |= (uint32_t)1u << i;
		}
	}

	return selected;
}

#ifdef __cplusplus
}
#endif

#endif /* SOC_TELINK_TLSR825X_DMA_IRQ_H_ */
