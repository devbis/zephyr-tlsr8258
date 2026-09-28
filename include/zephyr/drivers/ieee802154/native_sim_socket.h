/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief native_sim socket IEEE 802.15.4 driver extensions
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_H_
#define ZEPHYR_INCLUDE_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read pending datagrams from the medium socket and deliver them.
 *
 * The driver has no receive thread: the socket is read here and from the
 * driver's own TX and CCA paths. Call it periodically from the thread that
 * consumes received frames, never from interrupt context.
 */
void ieee802154_native_sim_socket_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_H_ */
