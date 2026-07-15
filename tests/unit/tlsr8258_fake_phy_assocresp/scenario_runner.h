/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TESTS_UNIT_TLSR8258_FAKE_PHY_ASSOCRESP_SCENARIO_RUNNER_H_
#define TESTS_UNIT_TLSR8258_FAKE_PHY_ASSOCRESP_SCENARIO_RUNNER_H_

#include <stddef.h>
#include <stdint.h>

#include "fake_phy_backend.h"

enum fake_phy_step_kind {
	FAKE_PHY_STEP_ADVANCE = 0,
	FAKE_PHY_STEP_MODE_RX,
	FAKE_PHY_STEP_TX_DONE,
	FAKE_PHY_STEP_OCCUPY_RX_BUFFER,
	FAKE_PHY_STEP_RAW_RX_IRQ,
	FAKE_PHY_STEP_RX_DMA_HANDOFF,
	FAKE_PHY_STEP_RX_ONLY_TX_COMPLETE,
	FAKE_PHY_STEP_RX_FRAME,
};

struct fake_phy_script_step {
	enum fake_phy_step_kind kind;
	uint16_t irq_status;
	int has_rx;
	int ack_tx_pending;
	int op_state_is_tx_pending;
	int dma_valid;
	uint8_t dma_len;
	const uint8_t *psdu;
	uint8_t len;
	uint32_t advance_cycles;
};

struct fake_phy_run_result {
	size_t tx_done_count;
	size_t raw_rx_irq_count;
	size_t invalid_dma_rx_count;
	size_t rx_dma_handoff_count;
	size_t rx_frame_count;
	size_t assoc_resp_to_ieee_count;
	size_t ack_kick_count;
	size_t ack_tx_completion_count;
	size_t stack_tx_complete_count;
	size_t rx_only_tx_complete_count;
	size_t pending_response_count;
	struct tlsr8258_core_tx_done_result last_tx_done;
	struct tlsr8258_core_rx_only_tx_result last_rx_only_tx;
	struct tlsr8258_core_rx_result last_rx;
};

void fake_phy_run_script(struct fake_phy_backend *phy, uint8_t tx_seq,
			 const struct tlsr8258_core_filter_ctx *filter,
			 const struct fake_phy_script_step *steps, size_t step_count,
			 struct fake_phy_run_result *result);

#endif
