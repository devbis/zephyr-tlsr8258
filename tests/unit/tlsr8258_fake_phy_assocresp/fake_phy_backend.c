/* SPDX-License-Identifier: Apache-2.0 */

#include "fake_phy_backend.h"

#include <string.h>

static void fake_phy_record_event(struct fake_phy_backend *phy, enum fake_phy_event_kind kind,
				      uint8_t seq, uint8_t len)
{
	if (phy->event_count >= FAKE_PHY_EVENT_LOG_CAP) {
		return;
	}

	phy->events[phy->event_count++] = (struct fake_phy_event){
		.kind = kind,
		.at_cycles = phy->now_cycles,
		.seq = seq,
		.len = len,
	};
}

static uint16_t fake_phy_get_irq_status(void *ctx)
{
	struct fake_phy_backend *phy = ctx;

	return phy->irq_status;
}

static void fake_phy_clear_irq_status(void *ctx, uint16_t bits)
{
	struct fake_phy_backend *phy = ctx;

	phy->irq_status &= (uint16_t)~bits;
}

static void fake_phy_set_mode_rx(void *ctx)
{
	struct fake_phy_backend *phy = ctx;

	phy->mode = FAKE_PHY_MODE_RX;
	fake_phy_record_event(phy, FAKE_PHY_EVENT_MODE_RX, 0u, 0u);
}

static void fake_phy_set_mode_rx_fast(void *ctx)
{
	struct fake_phy_backend *phy = ctx;

	phy->mode = FAKE_PHY_MODE_RX_FAST;
	fake_phy_record_event(phy, FAKE_PHY_EVENT_MODE_RX_FAST, 0u, 0u);
}

static void fake_phy_set_mode_tx(void *ctx)
{
	struct fake_phy_backend *phy = ctx;

	phy->mode = FAKE_PHY_MODE_TX;
}

static void fake_phy_set_rx_buffer(void *ctx, uint8_t *buf, uint16_t len)
{
	struct fake_phy_backend *phy = ctx;

	phy->current_rx_buffer = buf;
	phy->current_rx_buffer_len = len;
	phy->current_rx_buffer_occupied = false;
	fake_phy_record_event(phy, FAKE_PHY_EVENT_SET_RX_BUFFER, 0u, (uint8_t)len);
}

static void fake_phy_tx_start_ack(void *ctx, const uint8_t *ack, uint8_t len)
{
	struct fake_phy_backend *phy = ctx;

	phy->mode = FAKE_PHY_MODE_TX;
	phy->ack_len = len;
	if ((ack != NULL) && (len <= sizeof(phy->ack_psdu))) {
		memcpy(phy->ack_psdu, ack, len);
	}

	fake_phy_record_event(phy, FAKE_PHY_EVENT_MODE_TX_START_ACK,
			      (len >= 3u) ? ack[2] : 0u, len);
}

static uint32_t fake_phy_now_cycles(void *ctx)
{
	struct fake_phy_backend *phy = ctx;

	return phy->now_cycles;
}

void fake_phy_backend_init(struct fake_phy_backend *phy)
{
	memset(phy, 0, sizeof(*phy));
	phy->ops = (struct tlsr8258_radio_backend){
		.get_irq_status = fake_phy_get_irq_status,
		.clear_irq_status = fake_phy_clear_irq_status,
		.set_mode_rx = fake_phy_set_mode_rx,
		.set_mode_rx_fast = fake_phy_set_mode_rx_fast,
		.set_mode_tx = fake_phy_set_mode_tx,
		.set_rx_buffer = fake_phy_set_rx_buffer,
		.tx_start_ack = fake_phy_tx_start_ack,
		.now_cycles = fake_phy_now_cycles,
	};
	phy->current_rx_buffer = phy->rx_buffers[0];
	phy->rx_proc_buffer = NULL;
	phy->current_rx_buffer_len = sizeof(phy->rx_buffers[0]);
	phy->current_rx_buffer_occupied = false;
}

void fake_phy_backend_advance(struct fake_phy_backend *phy, uint32_t cycles)
{
	phy->now_cycles += cycles;
}

void fake_phy_backend_rx_dma_handoff(struct fake_phy_backend *phy)
{
	struct tlsr8258_core_rx_dma_result result;

	tlsr8258_core_handle_rx_dma(phy->current_rx_buffer, phy->rx_buffers[0], phy->rx_buffers[1],
				    &result);
	phy->rx_proc_buffer = result.rx_proc;
	phy->current_rx_buffer = result.next_rx_active;
	phy->current_rx_buffer_occupied = false;
	phy->current_rx_buffer[0] = 0u;
	phy->current_rx_buffer[4] = 0u;
	fake_phy_record_event(phy, FAKE_PHY_EVENT_RX_DMA_HANDOFF, 0u, 0u);
	fake_phy_set_rx_buffer(phy, phy->current_rx_buffer, sizeof(phy->rx_buffers[0]));
}

void fake_phy_backend_occupy_current_rx_buffer(struct fake_phy_backend *phy, uint8_t dma_len,
					       uint8_t psdu_len)
{
	phy->current_rx_buffer_occupied = true;
	phy->current_rx_buffer[0] = dma_len;
	phy->current_rx_buffer[4] = psdu_len;
	fake_phy_record_event(phy, FAKE_PHY_EVENT_OCCUPY_RX_BUFFER, 0u, psdu_len);
}

void fake_phy_backend_raw_rx_irq(struct fake_phy_backend *phy, bool dma_valid)
{
	if (!dma_valid) {
		fake_phy_record_event(phy, FAKE_PHY_EVENT_INVALID_DMA_RX, 0u, 0u);
		return;
	}

	if (!phy->current_rx_buffer_occupied) {
		fake_phy_record_event(phy, FAKE_PHY_EVENT_RAW_RX_IRQ, 0u, 0u);
		return;
	}

	fake_phy_backend_rx_dma_handoff(phy);
}
