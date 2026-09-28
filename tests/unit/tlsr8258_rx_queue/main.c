/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "ieee802154_tlsr8258_rx_queue.h"

ZTEST(tlsr8258_rx_queue, test_init_rejects_bad_geometry)
{
	struct tlsr8258_rx_slot slots[4] = { 0 };
	struct tlsr8258_rx_queue queue;

	zassert_false(tlsr8258_rx_queue_init(&queue, NULL, 2u));
	zassert_false(tlsr8258_rx_queue_init(&queue, slots, 0u));
	zassert_false(tlsr8258_rx_queue_init(&queue, slots, 3u));
	zassert_true(tlsr8258_rx_queue_init(&queue, slots, 4u));
}

ZTEST(tlsr8258_rx_queue, test_enqueue_dequeue_round_trip)
{
	struct tlsr8258_rx_slot slots[2] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;
	const uint8_t payload[] = { 1u, 2u, 3u, 4u };

	zassert_true(tlsr8258_rx_queue_init(&queue, slots, 2u));

	zassert_true(tlsr8258_rx_queue_try_enqueue(&queue, payload, sizeof(payload), -42));
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 1u);

	zassert_true(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	zassert_equal(frame.slot, &slots[0]);
	zassert_equal(frame.dma, slots[0].dma);
	zassert_equal(frame.len, sizeof(payload));
	zassert_equal(frame.rssi_dbm, -42);
	zassert_equal(memcmp(frame.dma, payload, sizeof(payload)), 0);

	/* A dequeued frame keeps its slot until it is released. */
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 1u);
	tlsr8258_rx_queue_release(&queue, frame.slot);
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 0u);
	zassert_false(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
}

ZTEST(tlsr8258_rx_queue, test_full_queue_drops_without_mutating)
{
	struct tlsr8258_rx_slot slots[1] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;
	const uint8_t first[] = { 0xa1u, 0xb2u };
	const uint8_t second[] = { 0xc3u };

	zassert_true(tlsr8258_rx_queue_init(&queue, slots, 1u));

	zassert_true(tlsr8258_rx_queue_try_enqueue(&queue, first, sizeof(first), -7));
	zassert_false(tlsr8258_rx_queue_try_enqueue(&queue, second, sizeof(second), -9));
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 1u);
	zassert_equal(tlsr8258_rx_queue_drop_count(&queue), 1u);

	zassert_true(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	zassert_equal(frame.len, sizeof(first));
	zassert_equal(frame.rssi_dbm, -7);
	zassert_equal(memcmp(frame.dma, first, sizeof(first)), 0);
	tlsr8258_rx_queue_release(&queue, frame.slot);
}

ZTEST(tlsr8258_rx_queue, test_oversized_frame_is_dropped)
{
	static const uint8_t big[TLSR8258_RX_SLOT_DMA_SIZE + 1u];
	struct tlsr8258_rx_slot slots[2] = { 0 };
	struct tlsr8258_rx_queue queue;

	zassert_true(tlsr8258_rx_queue_init(&queue, slots, 2u));
	zassert_false(tlsr8258_rx_queue_try_enqueue(&queue, big, sizeof(big), 0));
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 0u);
	zassert_equal(tlsr8258_rx_queue_drop_count(&queue), 1u);
}

ZTEST(tlsr8258_rx_queue, test_invalid_arguments)
{
	struct tlsr8258_rx_slot slots[1] = { 0 };
	struct tlsr8258_rx_queue queue;
	const uint8_t payload[] = { 0x55u };

	zassert_true(tlsr8258_rx_queue_init(&queue, slots, 1u));
	zassert_false(tlsr8258_rx_queue_try_enqueue(&queue, NULL, sizeof(payload), -11));
	zassert_equal(tlsr8258_rx_queue_drop_count(&queue), 0u);

	zassert_true(tlsr8258_rx_queue_try_enqueue(&queue, payload, sizeof(payload), -13));
	zassert_false(tlsr8258_rx_queue_try_dequeue(&queue, NULL));
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 1u);

	zassert_false(tlsr8258_rx_queue_init(&queue, NULL, 0u));
	zassert_false(tlsr8258_rx_queue_try_enqueue(&queue, payload, sizeof(payload), -12));
	zassert_equal(tlsr8258_rx_queue_drop_count(&queue), 0u);
}

ZTEST(tlsr8258_rx_queue, test_release_of_wrong_slot_is_ignored)
{
	struct tlsr8258_rx_slot slots[2] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;
	const uint8_t payload[] = { 0x66u };

	zassert_true(tlsr8258_rx_queue_init(&queue, slots, 2u));
	zassert_true(tlsr8258_rx_queue_try_enqueue(&queue, payload, sizeof(payload), 0));
	zassert_true(tlsr8258_rx_queue_try_dequeue(&queue, &frame));

	tlsr8258_rx_queue_release(&queue, &slots[1]);
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 1u);
	tlsr8258_rx_queue_release(&queue, frame.slot);
	zassert_equal(tlsr8258_rx_queue_pending(&queue), 0u);
}

ZTEST(tlsr8258_rx_queue, test_wraparound_preserves_fifo_order)
{
	struct tlsr8258_rx_slot slots[2] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;
	uint8_t payload[3];

	zassert_true(tlsr8258_rx_queue_init(&queue, slots, 2u));

	/* Run the free-running counters past their 8-bit wrap. */
	for (unsigned int i = 0u; i < 300u; i++) {
		payload[0] = (uint8_t)i;
		zassert_true(tlsr8258_rx_queue_try_enqueue(&queue, payload, 1u, (int8_t)-1));
		payload[0] = (uint8_t)(i + 1u);
		zassert_true(tlsr8258_rx_queue_try_enqueue(&queue, payload, 1u, (int8_t)-2));
		zassert_false(tlsr8258_rx_queue_try_enqueue(&queue, payload, 1u, 0));

		zassert_true(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
		zassert_equal(frame.dma[0], (uint8_t)i);
		zassert_equal(frame.rssi_dbm, -1);
		tlsr8258_rx_queue_release(&queue, frame.slot);

		zassert_true(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
		zassert_equal(frame.dma[0], (uint8_t)(i + 1u));
		zassert_equal(frame.rssi_dbm, -2);
		tlsr8258_rx_queue_release(&queue, frame.slot);

		zassert_equal(tlsr8258_rx_queue_pending(&queue), 0u);
	}

	zassert_equal(tlsr8258_rx_queue_drop_count(&queue), 300u);
}

ZTEST_SUITE(tlsr8258_rx_queue, NULL, NULL, NULL, NULL, NULL);
