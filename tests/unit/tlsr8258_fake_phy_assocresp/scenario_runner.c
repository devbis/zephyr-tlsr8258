/* SPDX-License-Identifier: Apache-2.0 */

#include "scenario_runner.h"

#include <string.h>

static void fake_phy_send_ack_if_needed(struct fake_phy_backend *phy,
					const struct tlsr8258_core_rx_result *rx)
{
	uint8_t ack_psdu[3];

	if (!rx->ack_eligible) {
		return;
	}

	ack_psdu[0] = 0x02u;
	ack_psdu[1] = 0x00u;
	ack_psdu[2] = rx->ack_seq;
	phy->ops.set_mode_tx(phy);
	phy->ops.tx_start_ack(phy, ack_psdu, sizeof(ack_psdu));
}

static void fake_phy_rearm_rx_buffer_if_needed(struct fake_phy_backend *phy,
					       const struct tlsr8258_core_tx_done_result *tx_done)
{
	uint8_t *next_buf;

	if (!tx_done->rearm_rx_buffer) {
		return;
	}

	next_buf = tlsr8258_core_next_rx_buffer(phy->current_rx_buffer, phy->rx_buffers[0],
						 phy->rx_buffers[1]);
	next_buf[0] = 0u;
	next_buf[4] = 0u;
	phy->ops.set_rx_buffer(phy, next_buf, sizeof(phy->rx_buffers[0]));
}

void fake_phy_run_script(struct fake_phy_backend *phy, uint8_t tx_seq,
			 const struct tlsr8258_core_filter_ctx *filter,
			 const struct fake_phy_script_step *steps, size_t step_count,
			 struct fake_phy_run_result *result)
{
	size_t i;

	memset(result, 0, sizeof(*result));
	for (i = 0; i < step_count; i++) {
		const struct fake_phy_script_step *step = &steps[i];

		switch (step->kind) {
		case FAKE_PHY_STEP_ADVANCE:
			fake_phy_backend_advance(phy, step->advance_cycles);
			break;
		case FAKE_PHY_STEP_MODE_RX:
			phy->ops.set_mode_rx(phy);
			break;
		case FAKE_PHY_STEP_TX_DONE:
		{
			bool expect_post_tx_rx = step->psdu != NULL;

			tlsr8258_core_handle_tx_done(step->irq_status, step->has_rx != 0,
						     step->ack_tx_pending != 0,
						     expect_post_tx_rx,
						     step->op_state_is_tx_pending != 0,
						     &result->last_tx_done);
			result->tx_done_count++;
			if (result->last_tx_done.count_ack_tx_completion) {
				result->ack_tx_completion_count++;
			}
			if (result->last_tx_done.complete_stack_tx) {
				result->stack_tx_complete_count++;
			}
			if (result->last_tx_done.enter_rx_fast) {
				phy->ops.set_mode_rx_fast(phy);
			}
			fake_phy_rearm_rx_buffer_if_needed(phy, &result->last_tx_done);
			break;
		}
		case FAKE_PHY_STEP_OCCUPY_RX_BUFFER:
			fake_phy_backend_occupy_current_rx_buffer(phy, step->dma_len, step->len);
			break;
		case FAKE_PHY_STEP_RAW_RX_IRQ:
			fake_phy_backend_raw_rx_irq(phy, step->dma_valid != 0);
			if (step->dma_valid != 0) {
				if (phy->event_count >= 2 &&
				    phy->events[phy->event_count - 2].kind ==
					    FAKE_PHY_EVENT_RX_DMA_HANDOFF) {
					result->rx_dma_handoff_count++;
				} else {
					result->raw_rx_irq_count++;
				}
			} else {
				result->invalid_dma_rx_count++;
			}
			break;
		case FAKE_PHY_STEP_RX_DMA_HANDOFF:
			fake_phy_backend_rx_dma_handoff(phy);
			result->rx_dma_handoff_count++;
			break;
		case FAKE_PHY_STEP_RX_ONLY_TX_COMPLETE:
			tlsr8258_core_handle_rx_only_tx_completion(step->has_rx != 0,
								   step->op_state_is_tx_pending != 0,
								   &result->last_rx_only_tx);
			if (result->last_rx_only_tx.complete_stack_tx) {
				result->stack_tx_complete_count++;
				result->rx_only_tx_complete_count++;
			}
			break;
		case FAKE_PHY_STEP_RX_FRAME:
			tlsr8258_core_handle_rx_frame(step->psdu, step->len, tx_seq, filter,
						      &result->last_rx);
			result->rx_frame_count++;
			if (result->last_rx.assoc_resp_to_ieee) {
				result->assoc_resp_to_ieee_count++;
			}
			if (result->last_rx.is_pending_response) {
				result->pending_response_count++;
			}
			if (result->last_rx.ack_eligible) {
				fake_phy_send_ack_if_needed(phy, &result->last_rx);
				result->ack_kick_count++;
			}
			break;
		}
	}
}
