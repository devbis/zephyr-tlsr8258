/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "scenario_runner.h"

#define BIT(n) (1u << (n))
#define RF_IRQ_TX_DS BIT(8)

static int failures;

#define EXPECT_EQ(actual, expected) do { \
	unsigned int _actual = (unsigned int)(actual); \
	unsigned int _expected = (unsigned int)(expected); \
	if (_actual != _expected) { \
		fprintf(stderr, "FAIL %s:%d: %s=0x%x expected 0x%x\n", __FILE__, __LINE__, \
			#actual, _actual, _expected); \
		failures++; \
	} \
} while (0)

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		fprintf(stderr, "FAIL %s:%d expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static const uint8_t our_pan[2] = {0xff, 0xff};
static const uint8_t our_short[2] = {0xff, 0xff};
static const uint8_t our_ieee[8] = {0x0c, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1, 0xa4};
static const uint8_t other_ieee_assoc_resp[] = {
	0x63, 0xcc, 0x7f, 0x62, 0x1a, 0x99, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1,
	0xa4, 0x0c, 0x80, 0x1e, 0xfe, 0xff, 0x16, 0xa7, 0x20, 0x02, 0x5a, 0x22, 0x00,
};
static const uint8_t assoc_resp_to_us[] = {
	0x63, 0xcc, 0x7f, 0x62, 0x1a, 0x0c, 0x00, 0x02, 0x50, 0xe0, 0x38, 0xc1,
	0xa4, 0x0c, 0x80, 0x1e, 0xfe, 0xff, 0x16, 0xa7, 0x20, 0x02, 0x5a, 0x22, 0x00,
};
static const uint8_t coord_ack_to_poll[] = {0x02, 0x10, 0x7f};
static const uint8_t data_req_like_psdu[] = {0x63, 0x88, 0xa1, 0xce, 0x6a, 0x00, 0x00, 0x09, 0x99,
					     0x04};
static const uint8_t beacon_req_like_psdu[] = {0x03, 0x08, 0x62, 0xff, 0xff, 0xff, 0xff, 0x07};

static struct tlsr8258_core_filter_ctx test_filter(void)
{
	return (struct tlsr8258_core_filter_ctx){
		.pan_id = our_pan,
		.short_addr = our_short,
		.ieee_addr = our_ieee,
	};
}

static void test_fake_backend_starts_idle(void)
{
	struct fake_phy_backend phy;

	fake_phy_backend_init(&phy);
	EXPECT_EQ(phy.mode, FAKE_PHY_MODE_OFF);
	EXPECT_EQ(phy.event_count, 0u);
	EXPECT_EQ(phy.now_cycles, 0u);
	EXPECT_TRUE(phy.current_rx_buffer == phy.rx_buffers[0]);
	EXPECT_TRUE(phy.rx_proc_buffer == NULL);
}

static void test_poll_tx_done_then_assoc_resp_to_us_kicks_mac_ack(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS,
		  .op_state_is_tx_pending = 1, .psdu = data_req_like_psdu,
		  .len = sizeof(data_req_like_psdu) },
		{ .kind = FAKE_PHY_STEP_ADVANCE, .advance_cycles = 260u },
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = assoc_resp_to_us, .len = sizeof(assoc_resp_to_us) },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_tx_done.tx_success);
	EXPECT_FALSE(result.last_tx_done.tx_had_rx);
	EXPECT_TRUE(result.last_tx_done.enter_rx_fast);
	EXPECT_TRUE(result.last_tx_done.rearm_rx_buffer);
	EXPECT_FALSE(result.last_tx_done.count_ack_tx_completion);
	EXPECT_TRUE(result.last_tx_done.defer_stack_tx_to_rx_path);
	EXPECT_FALSE(result.last_tx_done.complete_stack_tx);
	EXPECT_EQ(result.tx_done_count, 1u);
	EXPECT_EQ(result.stack_tx_complete_count, 0u);
	EXPECT_EQ(result.rx_frame_count, 1u);
	EXPECT_EQ(result.assoc_resp_to_ieee_count, 1u);
	EXPECT_EQ(result.ack_kick_count, 1u);
	EXPECT_TRUE(result.last_rx.ack_requested);
	EXPECT_TRUE(result.last_rx.ack_eligible);
	EXPECT_FALSE(result.last_rx.is_ack);
	EXPECT_TRUE(result.last_rx.assoc_resp_to_ieee);
	EXPECT_EQ(phy.event_count, 3u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_MODE_RX_FAST);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
	EXPECT_EQ(phy.events[2].kind, FAKE_PHY_EVENT_MODE_TX_START_ACK);
	EXPECT_EQ(phy.events[2].at_cycles, 260u);
	EXPECT_EQ(phy.ack_len, 3u);
	EXPECT_EQ(phy.ack_psdu[0], 0x02u);
	EXPECT_EQ(phy.ack_psdu[2], 0x7fu);
	EXPECT_TRUE(phy.current_rx_buffer == phy.rx_buffers[1]);
}

static void test_rx_dma_handoff_swaps_proc_and_rearms_next_buffer(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_RX_DMA_HANDOFF },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_EQ(result.rx_dma_handoff_count, 1u);
	EXPECT_TRUE(phy.rx_proc_buffer == phy.rx_buffers[0]);
	EXPECT_TRUE(phy.current_rx_buffer == phy.rx_buffers[1]);
	EXPECT_EQ(phy.event_count, 2u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_RX_DMA_HANDOFF);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
}

static void test_raw_rx_irq_with_invalid_dma_never_reaches_dma_handoff(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_RAW_RX_IRQ, .dma_valid = 0 },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_EQ(result.invalid_dma_rx_count, 1u);
	EXPECT_EQ(result.rx_dma_handoff_count, 0u);
	EXPECT_EQ(phy.event_count, 1u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_INVALID_DMA_RX);
	EXPECT_TRUE(phy.rx_proc_buffer == NULL);
	EXPECT_TRUE(phy.current_rx_buffer == phy.rx_buffers[0]);
}

static void test_raw_rx_irq_without_buffer_occupancy_never_reaches_dma_handoff(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_RAW_RX_IRQ, .dma_valid = 1 },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_EQ(result.raw_rx_irq_count, 1u);
	EXPECT_EQ(result.rx_dma_handoff_count, 0u);
	EXPECT_EQ(phy.event_count, 1u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_RAW_RX_IRQ);
	EXPECT_TRUE(phy.rx_proc_buffer == NULL);
}

static void test_raw_rx_irq_with_buffer_occupancy_promotes_to_dma_handoff(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_OCCUPY_RX_BUFFER, .dma_len = 13u, .len = 4u },
		{ .kind = FAKE_PHY_STEP_RAW_RX_IRQ, .dma_valid = 1 },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_EQ(result.raw_rx_irq_count, 0u);
	EXPECT_EQ(result.rx_dma_handoff_count, 1u);
	EXPECT_EQ(phy.event_count, 3u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_OCCUPY_RX_BUFFER);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_RX_DMA_HANDOFF);
	EXPECT_EQ(phy.events[2].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
	EXPECT_TRUE(phy.rx_proc_buffer == phy.rx_buffers[0]);
	EXPECT_TRUE(phy.current_rx_buffer == phy.rx_buffers[1]);
}

static void test_post_poll_occupied_buffer_model_matches_tx_done_rearm_hypothesis(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_OCCUPY_RX_BUFFER, .dma_len = 13u, .len = 3u },
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS,
		  .op_state_is_tx_pending = 1, .psdu = data_req_like_psdu,
		  .len = sizeof(data_req_like_psdu) },
		{ .kind = FAKE_PHY_STEP_OCCUPY_RX_BUFFER, .dma_len = 34u,
		  .len = (uint8_t)sizeof(assoc_resp_to_us) },
		{ .kind = FAKE_PHY_STEP_RAW_RX_IRQ, .dma_valid = 1 },
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = assoc_resp_to_us,
		  .len = sizeof(assoc_resp_to_us) },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_tx_done.rearm_rx_buffer);
	EXPECT_EQ(result.rx_dma_handoff_count, 1u);
	EXPECT_EQ(result.assoc_resp_to_ieee_count, 1u);
	EXPECT_EQ(result.ack_kick_count, 1u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_OCCUPY_RX_BUFFER);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_MODE_RX_FAST);
	EXPECT_EQ(phy.events[2].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
	EXPECT_EQ(phy.events[3].kind, FAKE_PHY_EVENT_OCCUPY_RX_BUFFER);
	EXPECT_EQ(phy.events[4].kind, FAKE_PHY_EVENT_RX_DMA_HANDOFF);
	EXPECT_EQ(phy.events[5].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
	EXPECT_EQ(phy.events[6].kind, FAKE_PHY_EVENT_MODE_TX_START_ACK);
}

static void test_on_air_assocresp_but_no_raw_rx_irq_matches_current_hw_failure_shape(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_OCCUPY_RX_BUFFER, .dma_len = 13u, .len = 3u },
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS,
		  .op_state_is_tx_pending = 1, .psdu = data_req_like_psdu,
		  .len = sizeof(data_req_like_psdu) },
		{ .kind = FAKE_PHY_STEP_ADVANCE, .advance_cycles = 260u },
		/* AssocResp exists on-air, but no raw RX IRQ reaches the driver. */
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_tx_done.tx_success);
	EXPECT_TRUE(result.last_tx_done.rearm_rx_buffer);
	EXPECT_EQ(result.raw_rx_irq_count, 0u);
	EXPECT_EQ(result.invalid_dma_rx_count, 0u);
	EXPECT_EQ(result.rx_dma_handoff_count, 0u);
	EXPECT_EQ(result.assoc_resp_to_ieee_count, 0u);
	EXPECT_EQ(result.ack_kick_count, 0u);
	EXPECT_EQ(phy.event_count, 3u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_OCCUPY_RX_BUFFER);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_MODE_RX_FAST);
	EXPECT_EQ(phy.events[2].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
}

static void test_on_air_assocresp_with_invalid_dma_rx_stops_before_handoff(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_OCCUPY_RX_BUFFER, .dma_len = 13u, .len = 3u },
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS,
		  .op_state_is_tx_pending = 1, .psdu = data_req_like_psdu,
		  .len = sizeof(data_req_like_psdu) },
		{ .kind = FAKE_PHY_STEP_ADVANCE, .advance_cycles = 260u },
		{ .kind = FAKE_PHY_STEP_RAW_RX_IRQ, .dma_valid = 0 },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_tx_done.rearm_rx_buffer);
	EXPECT_EQ(result.raw_rx_irq_count, 0u);
	EXPECT_EQ(result.invalid_dma_rx_count, 1u);
	EXPECT_EQ(result.rx_dma_handoff_count, 0u);
	EXPECT_EQ(result.assoc_resp_to_ieee_count, 0u);
	EXPECT_EQ(result.ack_kick_count, 0u);
	EXPECT_EQ(phy.event_count, 4u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_OCCUPY_RX_BUFFER);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_MODE_RX_FAST);
	EXPECT_EQ(phy.events[2].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
	EXPECT_EQ(phy.events[3].kind, FAKE_PHY_EVENT_INVALID_DMA_RX);
}

static void test_vendor_manual_tx_path_poll_ack_then_assoc_resp_still_kicks_ack(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_MODE_RX },
		{ .kind = FAKE_PHY_STEP_RX_DMA_HANDOFF },
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = coord_ack_to_poll,
		  .len = sizeof(coord_ack_to_poll) },
		{ .kind = FAKE_PHY_STEP_ADVANCE, .advance_cycles = 260u },
		{ .kind = FAKE_PHY_STEP_RX_DMA_HANDOFF },
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = assoc_resp_to_us,
		  .len = sizeof(assoc_resp_to_us) },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x7fu, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_EQ(result.tx_done_count, 0u);
	EXPECT_EQ(result.rx_dma_handoff_count, 2u);
	EXPECT_EQ(result.rx_frame_count, 2u);
	EXPECT_EQ(result.assoc_resp_to_ieee_count, 1u);
	EXPECT_EQ(result.ack_kick_count, 1u);
	EXPECT_TRUE(result.last_rx.ack_requested);
	EXPECT_TRUE(result.last_rx.ack_eligible);
	EXPECT_FALSE(result.last_rx.is_ack);
	EXPECT_EQ(phy.event_count, 6u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_MODE_RX);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_RX_DMA_HANDOFF);
	EXPECT_EQ(phy.events[2].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
	EXPECT_EQ(phy.events[3].kind, FAKE_PHY_EVENT_RX_DMA_HANDOFF);
	EXPECT_EQ(phy.events[4].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
	EXPECT_EQ(phy.events[5].kind, FAKE_PHY_EVENT_MODE_TX_START_ACK);
	EXPECT_EQ(phy.events[5].at_cycles, 260u);
	EXPECT_EQ(phy.ack_psdu[2], 0x7fu);
	EXPECT_TRUE(phy.rx_proc_buffer == phy.rx_buffers[1]);
	EXPECT_TRUE(phy.current_rx_buffer == phy.rx_buffers[0]);
}

static void test_coord_ack_is_seen_but_not_reacked(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = coord_ack_to_poll, .len = sizeof(coord_ack_to_poll) },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x7fu, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_EQ(result.rx_frame_count, 1u);
	EXPECT_TRUE(result.last_rx.is_ack);
	EXPECT_FALSE(result.last_rx.ack_requested);
	EXPECT_FALSE(result.last_rx.ack_eligible);
	EXPECT_EQ(result.ack_kick_count, 0u);
	EXPECT_EQ(phy.event_count, 0u);
}

static void test_assoc_resp_to_other_ieee_does_not_kick_ack(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = other_ieee_assoc_resp,
		  .len = sizeof(other_ieee_assoc_resp) },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_EQ(result.rx_frame_count, 1u);
	EXPECT_FALSE(result.last_rx.assoc_resp_to_ieee);
	EXPECT_FALSE(result.last_rx.ack_eligible);
	EXPECT_EQ(result.assoc_resp_to_ieee_count, 0u);
	EXPECT_EQ(result.ack_kick_count, 0u);
	EXPECT_EQ(phy.event_count, 0u);
}

static void test_missing_rx_event_distinguishes_rx_loss_from_ack_failure(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS,
		  .op_state_is_tx_pending = 1, .psdu = data_req_like_psdu,
		  .len = sizeof(data_req_like_psdu) },
		{ .kind = FAKE_PHY_STEP_ADVANCE, .advance_cycles = 260u },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_tx_done.enter_rx_fast);
	EXPECT_TRUE(result.last_tx_done.rearm_rx_buffer);
	EXPECT_TRUE(result.last_tx_done.defer_stack_tx_to_rx_path);
	EXPECT_FALSE(result.last_tx_done.complete_stack_tx);
	EXPECT_EQ(result.rx_frame_count, 0u);
	EXPECT_EQ(result.assoc_resp_to_ieee_count, 0u);
	EXPECT_EQ(result.ack_kick_count, 0u);
	EXPECT_EQ(phy.event_count, 2u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_MODE_RX_FAST);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
}

static void test_post_tx_rx_window_timeout_means_no_raw_rx_irq_arrived(void)
{
	struct tlsr8258_core_post_tx_rx_window_result result;

	tlsr8258_core_observe_post_tx_rx_window(true, false, false, 0u, 6000u, 6000u, &result);

	EXPECT_EQ(result.classification, TLSR8258_CORE_POST_TX_RX_NO_RAW_TIMEOUT);
	EXPECT_FALSE(result.window_armed_after);
	EXPECT_TRUE(result.timed_out_without_raw_rx);
	EXPECT_FALSE(result.saw_raw_rx);
	EXPECT_FALSE(result.saw_valid_rx_handoff);
}

static void test_post_tx_rx_window_raw_irq_drop_is_distinguished_from_no_irq(void)
{
	struct tlsr8258_core_post_tx_rx_window_result result;

	tlsr8258_core_observe_post_tx_rx_window(true, false, false, BIT(0), 260u, 6000u,
						 &result);

	EXPECT_EQ(result.classification, TLSR8258_CORE_POST_TX_RX_RAW_DROPPED);
	EXPECT_FALSE(result.window_armed_after);
	EXPECT_FALSE(result.timed_out_without_raw_rx);
	EXPECT_TRUE(result.saw_raw_rx);
	EXPECT_FALSE(result.saw_valid_rx_handoff);
}

static void test_post_tx_rx_window_valid_rx_handoff_is_distinguished_from_drop(void)
{
	struct tlsr8258_core_post_tx_rx_window_result result;

	tlsr8258_core_observe_post_tx_rx_window(true, false, true, BIT(0), 260u, 6000u,
						 &result);

	EXPECT_EQ(result.classification, TLSR8258_CORE_POST_TX_RX_RX_HANDOFF);
	EXPECT_FALSE(result.window_armed_after);
	EXPECT_FALSE(result.timed_out_without_raw_rx);
	EXPECT_TRUE(result.saw_raw_rx);
	EXPECT_TRUE(result.saw_valid_rx_handoff);
}

static void test_post_tx_rx_window_late_rx_does_not_count_as_in_window_handoff(void)
{
	struct tlsr8258_core_post_tx_rx_window_result result;

	tlsr8258_core_observe_post_tx_rx_window(true, false, true, BIT(0), 7000u, 6000u, &result);

	EXPECT_EQ(result.classification, TLSR8258_CORE_POST_TX_RX_NO_RAW_TIMEOUT);
	EXPECT_FALSE(result.window_armed_after);
	EXPECT_TRUE(result.timed_out_without_raw_rx);
	EXPECT_FALSE(result.saw_raw_rx);
	EXPECT_FALSE(result.saw_valid_rx_handoff);
}

static void test_post_tx_followup_candidate_matches_datareq_and_beaconreq(void)
{
	EXPECT_TRUE(tlsr8258_core_psdu_expects_post_tx_followup(data_req_like_psdu,
								 sizeof(data_req_like_psdu)));
	EXPECT_TRUE(tlsr8258_core_psdu_expects_post_tx_followup(beacon_req_like_psdu,
								 sizeof(beacon_req_like_psdu)));
	EXPECT_FALSE(tlsr8258_core_psdu_expects_post_tx_followup(assoc_resp_to_us,
								  sizeof(assoc_resp_to_us)));
}

static void test_combined_tx_rx_completion_does_not_fast_rearm_or_swap_buffer(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS, .has_rx = 1,
		  .op_state_is_tx_pending = 1, .psdu = assoc_resp_to_us },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_tx_done.tx_success);
	EXPECT_TRUE(result.last_tx_done.tx_had_rx);
	EXPECT_FALSE(result.last_tx_done.enter_rx_fast);
	EXPECT_FALSE(result.last_tx_done.rearm_rx_buffer);
	EXPECT_FALSE(result.last_tx_done.defer_stack_tx_to_rx_path);
	EXPECT_TRUE(result.last_tx_done.complete_stack_tx);
	EXPECT_EQ(result.stack_tx_complete_count, 1u);
	EXPECT_EQ(phy.event_count, 0u);
	EXPECT_TRUE(phy.current_rx_buffer == phy.rx_buffers[0]);
}

static void test_ack_only_tx_completion_clears_ack_pending_without_stack_tx_complete(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS,
		  .ack_tx_pending = 1, .op_state_is_tx_pending = 1 },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_tx_done.tx_success);
	EXPECT_TRUE(result.last_tx_done.clear_ack_tx_pending);
	EXPECT_TRUE(result.last_tx_done.count_ack_tx_completion);
	EXPECT_FALSE(result.last_tx_done.complete_stack_tx);
	EXPECT_EQ(result.ack_tx_completion_count, 1u);
	EXPECT_EQ(result.stack_tx_complete_count, 0u);
	EXPECT_EQ(phy.event_count, 2u);
	EXPECT_EQ(phy.events[0].kind, FAKE_PHY_EVENT_MODE_RX_FAST);
	EXPECT_EQ(phy.events[1].kind, FAKE_PHY_EVENT_SET_RX_BUFFER);
}

static void test_rx_only_completion_while_tx_pending_completes_stack_tx(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_RX_ONLY_TX_COMPLETE, .op_state_is_tx_pending = 1 },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_TRUE(result.last_rx_only_tx.saw_rx_while_tx_pending);
	EXPECT_TRUE(result.last_rx_only_tx.complete_stack_tx);
	EXPECT_EQ(result.rx_only_tx_complete_count, 1u);
	EXPECT_EQ(result.stack_tx_complete_count, 1u);
}

static void test_rx_only_completion_without_tx_pending_is_noop(void)
{
	struct fake_phy_backend phy;
	struct fake_phy_run_result result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step steps[] = {
		{ .kind = FAKE_PHY_STEP_RX_ONLY_TX_COMPLETE, .op_state_is_tx_pending = 0 },
	};

	fake_phy_backend_init(&phy);
	fake_phy_run_script(&phy, 0x23u, &filter, steps, sizeof(steps) / sizeof(steps[0]), &result);

	EXPECT_FALSE(result.last_rx_only_tx.saw_rx_while_tx_pending);
	EXPECT_FALSE(result.last_rx_only_tx.complete_stack_tx);
	EXPECT_EQ(result.rx_only_tx_complete_count, 0u);
	EXPECT_EQ(result.stack_tx_complete_count, 0u);
}

static void test_current_and_vendor_paths_only_differ_in_post_poll_event_shape(void)
{
	struct fake_phy_backend current_phy;
	struct fake_phy_backend vendor_phy;
	struct fake_phy_run_result current_result;
	struct fake_phy_run_result vendor_result;
	const struct tlsr8258_core_filter_ctx filter = test_filter();
	const struct fake_phy_script_step current_steps[] = {
		{ .kind = FAKE_PHY_STEP_TX_DONE, .irq_status = RF_IRQ_TX_DS,
		  .op_state_is_tx_pending = 1, .psdu = data_req_like_psdu,
		  .len = sizeof(data_req_like_psdu) },
		{ .kind = FAKE_PHY_STEP_ADVANCE, .advance_cycles = 260u },
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = assoc_resp_to_us,
		  .len = sizeof(assoc_resp_to_us) },
	};
	const struct fake_phy_script_step vendor_steps[] = {
		{ .kind = FAKE_PHY_STEP_MODE_RX },
		{ .kind = FAKE_PHY_STEP_RX_DMA_HANDOFF },
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = coord_ack_to_poll,
		  .len = sizeof(coord_ack_to_poll) },
		{ .kind = FAKE_PHY_STEP_ADVANCE, .advance_cycles = 260u },
		{ .kind = FAKE_PHY_STEP_RX_DMA_HANDOFF },
		{ .kind = FAKE_PHY_STEP_RX_FRAME, .psdu = assoc_resp_to_us,
		  .len = sizeof(assoc_resp_to_us) },
	};

	fake_phy_backend_init(&current_phy);
	fake_phy_backend_init(&vendor_phy);
	fake_phy_run_script(&current_phy, 0x23u, &filter, current_steps,
			    sizeof(current_steps) / sizeof(current_steps[0]), &current_result);
	fake_phy_run_script(&vendor_phy, 0x7fu, &filter, vendor_steps,
			    sizeof(vendor_steps) / sizeof(vendor_steps[0]), &vendor_result);

	EXPECT_EQ(current_result.assoc_resp_to_ieee_count, 1u);
	EXPECT_EQ(vendor_result.assoc_resp_to_ieee_count, 1u);
	EXPECT_EQ(current_result.ack_kick_count, 1u);
	EXPECT_EQ(vendor_result.ack_kick_count, 1u);
	EXPECT_EQ(current_phy.ack_psdu[2], vendor_phy.ack_psdu[2]);
	EXPECT_EQ(current_phy.events[0].kind, FAKE_PHY_EVENT_MODE_RX_FAST);
	EXPECT_EQ(vendor_phy.events[0].kind, FAKE_PHY_EVENT_MODE_RX);
	EXPECT_EQ(current_result.tx_done_count, 1u);
	EXPECT_EQ(vendor_result.tx_done_count, 0u);
	EXPECT_FALSE(current_result.last_tx_done.tx_had_rx);
	EXPECT_TRUE(current_result.last_tx_done.rearm_rx_buffer);
	EXPECT_TRUE(current_result.last_tx_done.defer_stack_tx_to_rx_path);
	EXPECT_EQ(current_result.stack_tx_complete_count, 0u);
	EXPECT_TRUE(current_phy.current_rx_buffer == current_phy.rx_buffers[1]);
	EXPECT_EQ(vendor_result.rx_dma_handoff_count, 2u);
	EXPECT_TRUE(vendor_phy.current_rx_buffer == vendor_phy.rx_buffers[0]);
}

int main(void)
{
	test_fake_backend_starts_idle();
	test_poll_tx_done_then_assoc_resp_to_us_kicks_mac_ack();
	test_rx_dma_handoff_swaps_proc_and_rearms_next_buffer();
	test_raw_rx_irq_with_invalid_dma_never_reaches_dma_handoff();
	test_raw_rx_irq_without_buffer_occupancy_never_reaches_dma_handoff();
	test_raw_rx_irq_with_buffer_occupancy_promotes_to_dma_handoff();
	test_post_poll_occupied_buffer_model_matches_tx_done_rearm_hypothesis();
	test_on_air_assocresp_but_no_raw_rx_irq_matches_current_hw_failure_shape();
	test_on_air_assocresp_with_invalid_dma_rx_stops_before_handoff();
	test_rx_only_completion_while_tx_pending_completes_stack_tx();
	test_rx_only_completion_without_tx_pending_is_noop();
	test_vendor_manual_tx_path_poll_ack_then_assoc_resp_still_kicks_ack();
	test_coord_ack_is_seen_but_not_reacked();
	test_assoc_resp_to_other_ieee_does_not_kick_ack();
	test_missing_rx_event_distinguishes_rx_loss_from_ack_failure();
	test_post_tx_rx_window_timeout_means_no_raw_rx_irq_arrived();
	test_post_tx_rx_window_raw_irq_drop_is_distinguished_from_no_irq();
	test_post_tx_rx_window_valid_rx_handoff_is_distinguished_from_drop();
	test_post_tx_rx_window_late_rx_does_not_count_as_in_window_handoff();
	test_post_tx_followup_candidate_matches_datareq_and_beaconreq();
	test_combined_tx_rx_completion_does_not_fast_rearm_or_swap_buffer();
	test_ack_only_tx_completion_clears_ack_pending_without_stack_tx_complete();
	test_current_and_vendor_paths_only_differ_in_post_poll_event_shape();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_fake_phy_assocresp: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_fake_phy_assocresp: PASS\n");
	return 0;
}
