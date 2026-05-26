/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_tlsr8258_rf_irq.h"

#define BIT(n) (1u << (n))

#define RF_IRQ_RX       BIT(0)
#define RF_IRQ_TX       BIT(1)

static bool tlsr8258_rf_dma_frame_valid(const uint8_t *rx, size_t rx_size)
{
	uint8_t dma_len;

	if ((rx == NULL) || (rx_size < 8u)) {
		return false;
	}

	dma_len = rx[0];
	if ((dma_len == 0u) || (dma_len >= (rx_size - 3u))) {
		return false;
	}

	if (dma_len != (uint8_t)(rx[4] + 9u)) {
		return false;
	}

	return (rx[dma_len + 3u] & 0x51u) == 0x10u;
}

uint16_t tlsr8258_rf_irq_runtime_mask(void)
{
	return RF_IRQ_RX | RF_IRQ_TX;
}

uint16_t tlsr8258_rf_irq_effective_status(uint16_t irq, const uint8_t *rx, size_t rx_size)
{
	if ((irq == 0u) && tlsr8258_rf_dma_frame_valid(rx, rx_size)) {
		return RF_IRQ_RX;
	}

	return irq;
}

bool tlsr8258_rf_irq_has_rx_event(uint16_t irq)
{
	return (irq & RF_IRQ_RX) != 0u;
}
