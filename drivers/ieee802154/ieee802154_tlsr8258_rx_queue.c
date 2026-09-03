/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_tlsr8258_rx_queue.h"

#include <string.h>

enum tlsr8258_rx_slot_state {
	TLSR8258_RX_SLOT_FREE = 0u,
	TLSR8258_RX_SLOT_READY = 1u,
	TLSR8258_RX_SLOT_INFLIGHT = 2u,
};

void tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count)
{
	queue->slots = slots;
	queue->slot_count = slot_count;
	queue->head = 0u;
	queue->tail = 0u;
	queue->pending = 0u;
	queue->drop_count = 0u;

	for (uint8_t i = 0u; i < slot_count; i++) {
		slots[i].len = 0u;
		slots[i].rssi_dbm = 0;
		slots[i].queued = false;
		slots[i].state = TLSR8258_RX_SLOT_FREE;
	}
}

bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue, const uint8_t *dma, uint8_t len,
				   int8_t rssi_dbm)
{
	struct tlsr8258_rx_slot *slot = NULL;
	uint8_t index;

	/* A malformed producer call is not queue overflow. */
	if ((queue == NULL) || (dma == NULL) || (queue->slot_count == 0u)) {
		return false;
	}

	/*
	 * The producer is the RF ISR and the consumer is the ZB loop.  A lost
	 * consumer pass (or an interrupt arriving between the consumer's head
	 * update and slot release) can leave tail pointing at a busy slot while a
	 * later slot is free.  The old single-slot check then drops every frame
	 * until reboot, even though the queue is not full.  Find the next free slot
	 * and keep tail immediately after it; the normal path still takes the first
	 * iteration and remains FIFO.
	 */
	for (uint8_t offset = 0u; offset < queue->slot_count; offset++) {
		index = (uint8_t)((queue->tail + offset) % queue->slot_count);
		if (queue->slots[index].state == TLSR8258_RX_SLOT_FREE) {
			slot = &queue->slots[index];
			queue->tail = (uint8_t)((index + 1u) % queue->slot_count);
			break;
		}
	}
	if (slot == NULL) {
		queue->drop_count++;
		return false;
	}

	memcpy(slot->dma, dma, len);
	slot->len = len;
	slot->rssi_dbm = rssi_dbm;
	/* The RF ISR is the sole producer.  Publish the payload before READY. */
	slot->state = TLSR8258_RX_SLOT_READY;
	slot->queued = true;
	queue->pending++;

	return true;
}

bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue,
					struct tlsr8258_rx_frame *frame)
{
	struct tlsr8258_rx_slot *slot = NULL;

	if ((queue == NULL) || (frame == NULL) || (queue->slot_count == 0u)) {
		return false;
	}

	/* Recover from a stale head left by an interrupted producer/consumer
	 * handoff.  Search in FIFO order; under normal operation offset is zero. */
	for (uint8_t offset = 0u; offset < queue->slot_count; offset++) {
		uint8_t index = (uint8_t)((queue->head + offset) % queue->slot_count);

		if (queue->slots[index].state == TLSR8258_RX_SLOT_READY) {
			slot = &queue->slots[index];
			queue->head = (uint8_t)((index + 1u) % queue->slot_count);
			break;
		}
	}
	if (slot == NULL) {
		return false;
	}

	frame->slot = slot;
	frame->dma = slot->dma;
	frame->len = slot->len;
	frame->rssi_dbm = slot->rssi_dbm;
	slot->state = TLSR8258_RX_SLOT_INFLIGHT;
	slot->queued = false;
	if (queue->pending != 0u) {
		queue->pending--;
	}

	return true;
}

void tlsr8258_rx_queue_release(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slot)
{
	(void)queue;

	if (slot == NULL) {
		return;
	}

	slot->state = TLSR8258_RX_SLOT_FREE;
}

uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue)
{
	return (queue != NULL) ? queue->drop_count : 0u;
}
