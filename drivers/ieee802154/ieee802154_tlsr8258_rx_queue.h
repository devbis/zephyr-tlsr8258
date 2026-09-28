/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_
#define ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_

#include <stdbool.h>
#include <stdint.h>

/* Size of the RF RX DMA buffer: DMA header, 127-byte PSDU and status trailer. */
#define TLSR8258_RX_SLOT_DMA_SIZE 144u

struct tlsr8258_rx_slot {
	uint8_t dma[TLSR8258_RX_SLOT_DMA_SIZE];
	uint8_t len;
	int8_t rssi_dbm;
};

struct tlsr8258_rx_frame {
	struct tlsr8258_rx_slot *slot;
	uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

/*
 * Single-producer single-consumer ring. The producer (RF ISR) is the only
 * writer of tail, the consumer (thread) the only writer of head. Both are
 * free-running counters; the fill level is tail - head, so slot_count must be
 * a power of two no larger than 128.
 */
struct tlsr8258_rx_queue {
	struct tlsr8258_rx_slot *slots;
	uint8_t slot_count;
	volatile uint8_t head;
	volatile uint8_t tail;
	volatile uint32_t drop_count;
};

/*
 * Initializes a queue over caller-provided slot storage.
 *
 * Returns false and leaves the queue unusable when slot_count is not a power
 * of two in 1..128 or slots is NULL.
 */
bool tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count);
/* Producer side. Frames longer than TLSR8258_RX_SLOT_DMA_SIZE are dropped. */
bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue, const uint8_t *dma, uint8_t len,
				   int8_t rssi_dbm);
/*
 * Consumer side. Returns the oldest frame without removing it; frame->dma
 * points into the slot and stays valid until tlsr8258_rx_queue_release() is
 * called for that slot.
 */
bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue,
				   struct tlsr8258_rx_frame *frame);
/* Consumer side. Returns the slot of the oldest frame to the producer. */
void tlsr8258_rx_queue_release(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slot);
uint8_t tlsr8258_rx_queue_pending(const struct tlsr8258_rx_queue *queue);
uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue);

#endif /* ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_ */
