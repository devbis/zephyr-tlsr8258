/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_
#define ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_

#include <stdbool.h>
#include <stdint.h>

#define TLSR8258_RX_SLOT_DMA_SIZE 256u

struct tlsr8258_rx_frame {
	uint8_t *dma;
	uint8_t len;
	int8_t rssi_dbm;
};

struct tlsr8258_rx_slot {
	uint8_t dma[TLSR8258_RX_SLOT_DMA_SIZE];
	uint8_t len;
	int8_t rssi_dbm;
	bool queued;
};

struct tlsr8258_rx_queue {
	struct tlsr8258_rx_slot *slots;
	uint8_t slot_count;
	uint8_t head;
	uint8_t tail;
	uint8_t pending;
	uint32_t drop_count;
};

void tlsr8258_rx_queue_init(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_slot *slots,
			    uint8_t slot_count);
bool tlsr8258_rx_queue_try_enqueue(struct tlsr8258_rx_queue *queue, const uint8_t *dma, uint8_t len,
				   int8_t rssi_dbm);
bool tlsr8258_rx_queue_try_dequeue(struct tlsr8258_rx_queue *queue, struct tlsr8258_rx_frame *frame);
uint32_t tlsr8258_rx_queue_drop_count(const struct tlsr8258_rx_queue *queue);

#endif /* ZEPHYR_DRIVERS_IEEE802154_TLSR8258_RX_QUEUE_H_ */
