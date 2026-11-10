/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief TLSR8269 IEEE 802.15.4 driver: receive path without an RX thread
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_IEEE802154_TLSR8269_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_IEEE802154_TLSR8269_H_

/**
 * @brief Set the function the RF interrupt calls for every queued frame
 *
 * Only available when CONFIG_IEEE802154_TLSR8269_RX_THREAD is disabled. The
 * function runs in interrupt context and must only wake the thread that calls
 * tlsr8269_rx_service().
 *
 * @param notify Function to call, or NULL for none.
 */
void tlsr8269_rx_set_notify(void (*notify)(void));

/**
 * @brief Hand the queued received frames to the network stack
 *
 * Only available when CONFIG_IEEE802154_TLSR8269_RX_THREAD is disabled. Call
 * it from a single thread; it does not block.
 */
void tlsr8269_rx_service(void);

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_IEEE802154_TLSR8269_H_ */
