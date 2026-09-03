/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_
#define ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_

#include <stdbool.h>
#include <stdint.h>

#define TLSR8258_RX_SLOT_DMA_SIZE 256u

struct tlsr8258_rx_slot;

struct tlsr8258_rx_frame {
	struct tlsr8258_rx_slot *slot;
	uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

struct tlsr8258_rx_slot {
	uint8_t dma[TLSR8258_RX_SLOT_DMA_SIZE];
	uint8_t len;
	int8_t rssi_dbm;
	volatile bool queued;
	/* FREE -> READY -> INFLIGHT -> FREE.  The producer is the RF ISR and
	 * the consumer is the Zigbee thread; keeping this state in the slot makes
	 * the ownership handoff explicit and avoids a kernel FIFO in the ISR. */
	volatile uint8_t state;
};

struct tlsr8258_rx_queue {
	struct tlsr8258_rx_slot *slots;
	uint8_t slot_count;
	uint8_t head;
	uint8_t tail;
	volatile uint8_t pending;
	volatile uint32_t drop_count;
};

/*
 * Initializes a queue over caller-provided slot storage.
 *
 * When slot_count > 0, slots must point to at least slot_count writable slots.
 * slots may be NULL only when slot_count is 0.
 */
void tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count);
bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue, const uint8_t *dma, uint8_t len,
				   int8_t rssi_dbm);
/*
 * Dequeues the oldest frame into queue-owned slot storage.
 *
 * On success, frame->dma points at the slot backing the returned frame. That
 * pointer remains valid until the slot is reused by a later successful enqueue
 * or the queue is reinitialized. Callers must copy out any data they need
 * before the slot is enqueued again.
 */
bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_frame *frame);
void tlsr8258_rx_queue_release(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slot);
uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue);

#endif /* ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_ */
