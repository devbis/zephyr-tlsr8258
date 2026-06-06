/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_NATIVE_SIM_SOCKET_MEDIUM_H_
#define ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_NATIVE_SIM_SOCKET_MEDIUM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_NATIVE_SIM_SOCKET_MEDIUM_VERSION         1U
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE  8U
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PSDU_SIZE   127U
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE 192U
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_BROADCAST_PAN   0xffffU
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_BROADCAST_SHORT 0xffffU

enum zb_native_sim_socket_medium_msg_type {
	ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO = 1,
	ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER = 2,
	ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX = 3,
	ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX = 4,
	ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS = 5,
};

struct zb_native_sim_socket_medium_msg {
	enum zb_native_sim_socket_medium_msg_type type;
	uint16_t node_id;
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t channel;
	int8_t tx_power_dbm;
	int8_t rssi_dbm;
	uint8_t lqi;
	bool rx_on;
	uint8_t ieee_addr[ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE];
	const uint8_t *psdu;
	size_t psdu_len;
};

struct zb_native_sim_socket_medium_peer {
	uint16_t node_id;
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t channel;
	bool rx_on;
	uint8_t ieee_addr[ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE];
};

int zb_native_sim_socket_medium_encode(uint8_t *buffer, size_t capacity,
				       const struct zb_native_sim_socket_medium_msg *msg,
				       size_t *encoded_len);
int zb_native_sim_socket_medium_decode(struct zb_native_sim_socket_medium_msg *msg,
				       const uint8_t *buffer, size_t len);
void zb_native_sim_socket_medium_peer_reset(struct zb_native_sim_socket_medium_peer *peer);
int zb_native_sim_socket_medium_peer_apply(struct zb_native_sim_socket_medium_peer *peer,
					   const struct zb_native_sim_socket_medium_msg *msg);
bool zb_native_sim_socket_medium_peer_accepts_psdu(
	const struct zb_native_sim_socket_medium_peer *peer,
	const uint8_t *psdu, size_t psdu_len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_ZIGBEE_INCLUDE_ZEPHYR_ZIGBEE_NATIVE_SIM_SOCKET_MEDIUM_H_ */
