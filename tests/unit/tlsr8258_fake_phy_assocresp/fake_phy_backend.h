/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TESTS_UNIT_TLSR8258_FAKE_PHY_ASSOCRESP_FAKE_PHY_BACKEND_H_
#define TESTS_UNIT_TLSR8258_FAKE_PHY_ASSOCRESP_FAKE_PHY_BACKEND_H_

#include <stddef.h>
#include <stdint.h>

#include "ieee802154_tlsr8258_fake_phy_core.h"

enum fake_phy_mode {
	FAKE_PHY_MODE_OFF = 0,
	FAKE_PHY_MODE_RX,
	FAKE_PHY_MODE_RX_FAST,
	FAKE_PHY_MODE_TX,
};

enum fake_phy_event_kind {
	FAKE_PHY_EVENT_MODE_RX = 0,
	FAKE_PHY_EVENT_MODE_RX_FAST,
	FAKE_PHY_EVENT_SET_RX_BUFFER,
	FAKE_PHY_EVENT_RX_DMA_HANDOFF,
	FAKE_PHY_EVENT_OCCUPY_RX_BUFFER,
	FAKE_PHY_EVENT_RAW_RX_IRQ,
	FAKE_PHY_EVENT_INVALID_DMA_RX,
	FAKE_PHY_EVENT_MODE_TX_START_ACK,
};

struct fake_phy_event {
	enum fake_phy_event_kind kind;
	uint32_t at_cycles;
	uint8_t seq;
	uint8_t len;
};

#define FAKE_PHY_EVENT_LOG_CAP 16

struct fake_phy_backend {
	struct tlsr8258_radio_backend ops;
	enum fake_phy_mode mode;
	uint32_t now_cycles;
	uint16_t irq_status;
	uint8_t rx_buffers[2][128];
	uint8_t *current_rx_buffer;
	uint8_t *rx_proc_buffer;
	uint16_t current_rx_buffer_len;
	bool current_rx_buffer_occupied;
	uint8_t ack_psdu[8];
	uint8_t ack_len;
	size_t event_count;
	struct fake_phy_event events[FAKE_PHY_EVENT_LOG_CAP];
};

void fake_phy_backend_init(struct fake_phy_backend *phy);
void fake_phy_backend_advance(struct fake_phy_backend *phy, uint32_t cycles);
void fake_phy_backend_rx_dma_handoff(struct fake_phy_backend *phy);
void fake_phy_backend_occupy_current_rx_buffer(struct fake_phy_backend *phy, uint8_t dma_len,
					       uint8_t psdu_len);
void fake_phy_backend_raw_rx_irq(struct fake_phy_backend *phy, bool dma_valid);

#endif
