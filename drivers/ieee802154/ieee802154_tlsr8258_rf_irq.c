/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_tlsr8258_rf_irq.h"

#define BIT(n) (1u << (n))

#define RF_IRQ_RX       BIT(0)
#define RF_IRQ_TX       BIT(1)
#define RF_IRQ_RX_CRC_2 BIT(4)
#define RF_IRQ_RX_DR    BIT(9)
#define RF_IRQ_RX_EVENTS (RF_IRQ_RX | RF_IRQ_RX_CRC_2 | RF_IRQ_RX_DR)

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
	return RF_IRQ_RX_EVENTS | RF_IRQ_TX;
}

uint16_t tlsr8258_rf_irq_effective_status(uint16_t irq, const uint8_t *rx, size_t rx_size)
{
	bool dma_valid = tlsr8258_rf_dma_frame_valid(rx, rx_size);

	if ((irq & RF_IRQ_RX_EVENTS) != 0u) {
		if (!dma_valid) {
			return irq & (uint16_t)~RF_IRQ_RX_EVENTS;
		}

		return (irq & (uint16_t)~RF_IRQ_RX_EVENTS) | RF_IRQ_RX;
	}

	if ((irq == 0u) && dma_valid) {
		return RF_IRQ_RX;
	}

	return irq;
}

bool tlsr8258_rf_irq_has_rx_event(uint16_t irq)
{
	return (irq & RF_IRQ_RX_EVENTS) != 0u;
}
