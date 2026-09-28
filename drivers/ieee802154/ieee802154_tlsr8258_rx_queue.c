/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ieee802154_tlsr8258_rx_queue.h"

#include <string.h>

/* Order the slot payload against the index update that publishes it. */
#define TLSR8258_RX_QUEUE_BARRIER() __asm__ volatile("" ::: "memory")

static inline uint8_t tlsr8258_rx_queue_index(const struct tlsr8258_rx_queue *queue,
					      uint8_t counter)
{
	return (uint8_t)(counter & (uint8_t)(queue->slot_count - 1u));
}

bool tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count)
{
	bool valid = (slots != NULL) && (slot_count != 0u) && (slot_count <= 128u) &&
		     ((slot_count & (uint8_t)(slot_count - 1u)) == 0u);

	queue->slots = valid ? slots : NULL;
	queue->slot_count = valid ? slot_count : 0u;
	queue->head = 0u;
	queue->tail = 0u;
	queue->drop_count = 0u;

	return valid;
}

bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue, const uint8_t *dma, uint8_t len,
				   int8_t rssi_dbm)
{
	struct tlsr8258_rx_slot *slot;
	uint8_t tail;

	if ((queue == NULL) || (dma == NULL) || (queue->slot_count == 0u)) {
		return false;
	}

	tail = queue->tail;
	if ((len > TLSR8258_RX_SLOT_DMA_SIZE) ||
	    ((uint8_t)(tail - queue->head) >= queue->slot_count)) {
		queue->drop_count++;
		return false;
	}

	slot = &queue->slots[tlsr8258_rx_queue_index(queue, tail)];
	memcpy(slot->dma, dma, len);
	slot->len = len;
	slot->rssi_dbm = rssi_dbm;
	TLSR8258_RX_QUEUE_BARRIER();
	queue->tail = (uint8_t)(tail + 1u);

	return true;
}

bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue,
				   struct tlsr8258_rx_frame *frame)
{
	struct tlsr8258_rx_slot *slot;
	uint8_t head;

	if ((queue == NULL) || (frame == NULL) || (queue->slot_count == 0u)) {
		return false;
	}

	head = queue->head;
	if (head == queue->tail) {
		return false;
	}
	TLSR8258_RX_QUEUE_BARRIER();

	slot = &queue->slots[tlsr8258_rx_queue_index(queue, head)];
	frame->slot = slot;
	frame->dma = slot->dma;
	frame->len = slot->len;
	frame->rssi_dbm = slot->rssi_dbm;

	return true;
}

void tlsr8258_rx_queue_release(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slot)
{
	uint8_t head;

	if ((queue == NULL) || (slot == NULL) || (queue->slot_count == 0u)) {
		return;
	}

	head = queue->head;
	if ((head == queue->tail) ||
	    (slot != &queue->slots[tlsr8258_rx_queue_index(queue, head)])) {
		return;
	}

	TLSR8258_RX_QUEUE_BARRIER();
	queue->head = (uint8_t)(head + 1u);
}

uint8_t tlsr8258_rx_queue_pending(const struct tlsr8258_rx_queue *queue)
{
	return (queue != NULL) ? (uint8_t)(queue->tail - queue->head) : 0u;
}

uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue)
{
	return (queue != NULL) ? queue->drop_count : 0u;
}
