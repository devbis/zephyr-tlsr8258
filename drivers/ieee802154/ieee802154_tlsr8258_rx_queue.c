/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_tlsr8258_rx_queue.h"

#include <string.h>

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
	}
}

bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue, const uint8_t *dma, uint8_t len,
				   int8_t rssi_dbm)
{
	struct tlsr8258_rx_slot *slot;

	if ((queue->slot_count == 0u) || (dma == NULL)) {
		return false;
	}

	if (queue->pending >= queue->slot_count) {
		queue->drop_count++;
		return false;
	}

	slot = &queue->slots[queue->tail];
	memcpy(slot->dma, dma, len);
	slot->len = len;
	slot->rssi_dbm = rssi_dbm;
	slot->queued = true;

	queue->tail = (uint8_t)((queue->tail + 1u) % queue->slot_count);
	queue->pending++;

	return true;
}

bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_frame *frame)
{
	struct tlsr8258_rx_slot *slot;

	if ((queue->pending == 0u) || (frame == NULL)) {
		return false;
	}

	slot = &queue->slots[queue->head];
	frame->dma = slot->dma;
	frame->len = slot->len;
	frame->rssi_dbm = slot->rssi_dbm;
	slot->queued = false;

	queue->head = (uint8_t)((queue->head + 1u) % queue->slot_count);
	queue->pending--;

	return true;
}

uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue)
{
	return queue->drop_count;
}
