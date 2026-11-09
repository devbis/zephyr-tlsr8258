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

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_TLSR8258_H_ */
