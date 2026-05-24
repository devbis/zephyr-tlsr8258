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

int main(void)
{
	test_enqueue_dequeue_round_trip();
	test_overflow_increments_drop_count_without_mutating_queue();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_rx_queue: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_rx_queue: PASS\n");
	return 0;
}
