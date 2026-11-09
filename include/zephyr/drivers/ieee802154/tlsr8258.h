/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Telink TLSR8258 IEEE 802.15.4 driver extensions
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the RF DMA header in front of the PSDU in a received frame. */
#define TLSR8258_RX_FRAME_PSDU_OFFSET 5U

/**
 * @brief Receive notification.
 *
 * Called from the radio interrupt each time a frame is queued for
 * tlsr8258_radio_rx_poll(). It must not block.
 */
typedef void (*tlsr8258_rx_notify_t)(void);

/**
 * @brief Install the receive notification.
 *
 * @param notify Callback to install, or NULL to remove it.
 */
void tlsr8258_radio_register_rx_notify(tlsr8258_rx_notify_t notify);

/**
 * @brief Drain the receive queue into the installed sink or the L2.
 *
 * Must be called from thread context. There is no receive worker thread in
 * the driver: the caller owns the only consumer of the queue.
 */
void tlsr8258_radio_rx_poll(void);

/**
 * @brief Set the address filter used for software ACKs and receive filtering.
 *
 * The values persist across radio re-initialization.
 *
 * @param pan_id PAN identifier.
 * @param short_addr Short address.
 * @param ieee_addr Extended address in little-endian byte order, or NULL to
 *                  keep the current one.
 */
void tlsr8258_radio_update_filters(uint16_t pan_id, uint16_t short_addr,
				   const uint8_t *ieee_addr);

/**
 * @brief Get the Frame Pending bit of the last ACK the radio received.
 *
 * The ACK of a transmitted frame arrives after tx() has returned, so its
 * Frame Pending bit cannot be part of the tx() result. The receive interrupt
 * records it instead.
 *
 * @param seq Sequence number of the frame the ACK must acknowledge.
 * @param pending Receives the Frame Pending bit.
 *
 * @retval true The last received ACK carried @p seq and @p pending is set.
 * @retval false No ACK with that sequence number has been received.
 */
bool tlsr8258_radio_ack_frame_pending_get(uint8_t seq, bool *pending);

/**
 * @brief Get the extended address of the radio.
 *
 * The address is read from the ieee_addr_partition factory data when the
 * network interface is initialized.
 *
 * @param ieee_addr Buffer for the address, 8 bytes, in over-the-air order.
 *
 * @retval 0 Address copied.
 * @retval -EAGAIN The interface is not initialized yet.
 */
int tlsr8258_radio_get_ieee_addr(uint8_t ieee_addr[8]);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_H_ */
