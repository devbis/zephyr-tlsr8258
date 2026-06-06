/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_BOTTOM_H_
#define ZEPHYR_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_BOTTOM_H_

#include <stdint.h>

int ieee802154_native_sim_socket_open(const char *host, uint16_t port);
int ieee802154_native_sim_socket_rx_ready(int fd);
long ieee802154_native_sim_socket_recv(int fd, void *buffer, unsigned long size);
long ieee802154_native_sim_socket_send(int fd, const void *buffer, unsigned long size);

#endif /* ZEPHYR_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_BOTTOM_H_ */
