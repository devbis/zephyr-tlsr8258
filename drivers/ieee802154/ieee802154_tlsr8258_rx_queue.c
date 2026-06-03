/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_tlsr8258_rx_queue.h"

#include <string.h>

void tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count)
{
	k_fifo_init(&queue->free_fifo);
	k_fifo_init(&queue->ready_fifo);
	atomic_clear(&queue->drop_count);

	for (uint8_t i = 0u; i < slot_count; i++) {
		slots[i].fifo_reserved = NULL;
		slots[i].len = 0u;
		slots[i].rssi_dbm = 0;
		k_fifo_put(&queue->free_fifo, &slots[i]);
	}
}

bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue, const uint8_t *dma, uint8_t len,
				   int8_t rssi_dbm)
{
	struct tlsr8258_rx_slot *slot;

	if (dma == NULL) {
		return false;
	}

	slot = k_fifo_get(&queue->free_fifo, K_NO_WAIT);
	if (slot == NULL) {
		atomic_inc(&queue->drop_count);
		return false;
	}

	memcpy(slot->dma, dma, len);
	slot->len = len;
	slot->rssi_dbm = rssi_dbm;
	k_fifo_put(&queue->ready_fifo, slot);

	return true;
}

static bool tlsr8258_rx_queue_dequeue(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_frame *frame,
				      k_timeout_t timeout)
{
	struct tlsr8258_rx_slot *slot;

	if (frame == NULL) {
		return false;
	}

	slot = k_fifo_get(&queue->ready_fifo, timeout);
	if (slot == NULL) {
		return false;
	}

	frame->slot = slot;
	frame->dma = slot->dma;
	frame->len = slot->len;
	frame->rssi_dbm = slot->rssi_dbm;

	return true;
}

bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_frame *frame)
{
	return tlsr8258_rx_queue_dequeue(queue, frame, K_NO_WAIT);
}

bool tlsr8258_rx_queue_wait_dequeue(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_frame *frame,
				    k_timeout_t timeout)
{
	return tlsr8258_rx_queue_dequeue(queue, frame, timeout);
}

void tlsr8258_rx_queue_release(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slot)
{
	if (slot == NULL) {
		return;
	}

	k_fifo_put(&queue->free_fifo, slot);
}

uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue)
{
	return (uint32_t)atomic_get(&queue->drop_count);
}
