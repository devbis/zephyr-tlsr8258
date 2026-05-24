/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../drivers/ieee802154/ieee802154_tlsr8258_rx_queue.h"

static int failures;

#define EXPECT_EQ(actual, expected) do { \
	long long _actual = (long long)(actual); \
	long long _expected = (long long)(expected); \
	if (_actual != _expected) { \
		fprintf(stderr, "FAIL %s:%d: %s=%lld expected %lld\n", __FILE__, __LINE__, \
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

static void test_enqueue_dequeue_round_trip(void)
{
	struct tlsr8258_rx_slot slots[2] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;
	const uint8_t payload[] = { 1u, 2u, 3u, 4u };

	tlsr8258_rx_queue_init(&queue, slots, 2u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_enqueue(&queue, payload, sizeof(payload), -42));
	EXPECT_EQ(queue.pending, 1u);
	EXPECT_TRUE(slots[0].queued);
	EXPECT_EQ(slots[0].len, sizeof(payload));
	EXPECT_EQ(memcmp(slots[0].dma, payload, sizeof(payload)), 0);

	EXPECT_TRUE(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	EXPECT_EQ(queue.pending, 0u);
	EXPECT_FALSE(slots[0].queued);
	EXPECT_EQ(frame.dma, slots[0].dma);
	EXPECT_EQ(frame.len, sizeof(payload));
	EXPECT_EQ(frame.rssi_dbm, -42);
	EXPECT_EQ(memcmp(frame.dma, payload, sizeof(payload)), 0);
}

static void test_overflow_increments_drop_count_without_mutating_queue(void)
{
	struct tlsr8258_rx_slot slots[1] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;
	const uint8_t first_payload[] = { 0xa1u, 0xb2u };
	const uint8_t second_payload[] = { 0xc3u };

	tlsr8258_rx_queue_init(&queue, slots, 1u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_enqueue(&queue, first_payload, sizeof(first_payload), -7));
	EXPECT_FALSE(tlsr8258_rx_queue_try_enqueue(&queue, second_payload, sizeof(second_payload), -9));
	EXPECT_EQ(queue.pending, 1u);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 1u);
	EXPECT_EQ(slots[0].len, sizeof(first_payload));
	EXPECT_EQ(slots[0].rssi_dbm, -7);
	EXPECT_EQ(memcmp(slots[0].dma, first_payload, sizeof(first_payload)), 0);

	EXPECT_TRUE(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	EXPECT_EQ(frame.len, sizeof(first_payload));
	EXPECT_EQ(frame.rssi_dbm, -7);
	EXPECT_EQ(memcmp(frame.dma, first_payload, sizeof(first_payload)), 0);
}

static void test_invalid_enqueue_does_not_increment_drop_count(void)
{
	struct tlsr8258_rx_slot slots[1] = { 0 };
	struct tlsr8258_rx_queue queue;
	const uint8_t payload[] = { 0x55u };

	tlsr8258_rx_queue_init(&queue, slots, 1u);

	EXPECT_FALSE(tlsr8258_rx_queue_try_enqueue(&queue, NULL, sizeof(payload), -11));
	EXPECT_EQ(queue.pending, 0u);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 0u);
	EXPECT_FALSE(slots[0].queued);

	tlsr8258_rx_queue_init(&queue, NULL, 0u);
	EXPECT_FALSE(tlsr8258_rx_queue_try_enqueue(&queue, payload, sizeof(payload), -12));
	EXPECT_EQ(queue.pending, 0u);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 0u);
}

static void test_dequeue_from_empty_queue_fails(void)
{
	struct tlsr8258_rx_slot slots[1] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;

	tlsr8258_rx_queue_init(&queue, slots, 1u);

	EXPECT_FALSE(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	EXPECT_EQ(queue.head, 0u);
	EXPECT_EQ(queue.tail, 0u);
	EXPECT_EQ(queue.pending, 0u);
	EXPECT_FALSE(slots[0].queued);
}

static void test_dequeue_rejects_null_output_argument(void)
{
	struct tlsr8258_rx_slot slots[1] = { 0 };
	struct tlsr8258_rx_queue queue;
	const uint8_t payload[] = { 0x66u };

	tlsr8258_rx_queue_init(&queue, slots, 1u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_enqueue(&queue, payload, sizeof(payload), -13));
	EXPECT_FALSE(tlsr8258_rx_queue_try_dequeue(&queue, NULL));
	EXPECT_EQ(queue.head, 0u);
	EXPECT_EQ(queue.tail, 0u);
	EXPECT_EQ(queue.pending, 1u);
	EXPECT_TRUE(slots[0].queued);
}

static void test_wraparound_preserves_fifo_order_and_state(void)
{
	struct tlsr8258_rx_slot slots[2] = { 0 };
	struct tlsr8258_rx_queue queue;
	struct tlsr8258_rx_frame frame;
	const uint8_t payload_a[] = { 0x10u };
	const uint8_t payload_b[] = { 0x20u, 0x21u };
	const uint8_t payload_c[] = { 0x30u, 0x31u, 0x32u };

	tlsr8258_rx_queue_init(&queue, slots, 2u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_enqueue(&queue, payload_a, sizeof(payload_a), -1));
	EXPECT_TRUE(tlsr8258_rx_queue_try_enqueue(&queue, payload_b, sizeof(payload_b), -2));
	EXPECT_EQ(queue.head, 0u);
	EXPECT_EQ(queue.tail, 0u);
	EXPECT_EQ(queue.pending, 2u);
	EXPECT_TRUE(slots[0].queued);
	EXPECT_TRUE(slots[1].queued);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 0u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	EXPECT_EQ(frame.dma, slots[0].dma);
	EXPECT_EQ(frame.len, sizeof(payload_a));
	EXPECT_EQ(frame.rssi_dbm, -1);
	EXPECT_EQ(memcmp(frame.dma, payload_a, sizeof(payload_a)), 0);
	EXPECT_EQ(queue.head, 1u);
	EXPECT_EQ(queue.tail, 0u);
	EXPECT_EQ(queue.pending, 1u);
	EXPECT_FALSE(slots[0].queued);
	EXPECT_TRUE(slots[1].queued);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 0u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_enqueue(&queue, payload_c, sizeof(payload_c), -3));
	EXPECT_EQ(queue.head, 1u);
	EXPECT_EQ(queue.tail, 1u);
	EXPECT_EQ(queue.pending, 2u);
	EXPECT_TRUE(slots[0].queued);
	EXPECT_TRUE(slots[1].queued);
	EXPECT_EQ(memcmp(slots[0].dma, payload_c, sizeof(payload_c)), 0);
	EXPECT_EQ(slots[0].len, sizeof(payload_c));
	EXPECT_EQ(slots[0].rssi_dbm, -3);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 0u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	EXPECT_EQ(frame.dma, slots[1].dma);
	EXPECT_EQ(frame.len, sizeof(payload_b));
	EXPECT_EQ(frame.rssi_dbm, -2);
	EXPECT_EQ(memcmp(frame.dma, payload_b, sizeof(payload_b)), 0);
	EXPECT_TRUE(slots[0].queued);
	EXPECT_FALSE(slots[1].queued);
	EXPECT_EQ(queue.head, 0u);
	EXPECT_EQ(queue.tail, 1u);
	EXPECT_EQ(queue.pending, 1u);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 0u);

	EXPECT_TRUE(tlsr8258_rx_queue_try_dequeue(&queue, &frame));
	EXPECT_EQ(frame.dma, slots[0].dma);
	EXPECT_EQ(frame.len, sizeof(payload_c));
	EXPECT_EQ(frame.rssi_dbm, -3);
	EXPECT_EQ(memcmp(frame.dma, payload_c, sizeof(payload_c)), 0);
	EXPECT_FALSE(slots[0].queued);
	EXPECT_FALSE(slots[1].queued);
	EXPECT_EQ(queue.head, 1u);
	EXPECT_EQ(queue.tail, 1u);
	EXPECT_EQ(queue.pending, 0u);
	EXPECT_EQ(tlsr8258_rx_queue_drop_count(&queue), 0u);
}

int main(void)
{
	test_enqueue_dequeue_round_trip();
	test_overflow_increments_drop_count_without_mutating_queue();
	test_invalid_enqueue_does_not_increment_drop_count();
	test_dequeue_from_empty_queue_fails();
	test_dequeue_rejects_null_output_argument();
	test_wraparound_preserves_fifo_order_and_state();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_rx_queue: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_rx_queue: PASS\n");
	return 0;
}
