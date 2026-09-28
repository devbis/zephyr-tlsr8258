/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Datagram format exchanged between the native_sim socket IEEE 802.15.4
 * driver and the host-side medium process.
 *
 * Every datagram has a 28-byte little-endian header followed by the payload:
 *   0  magic "BZSM" (0x4d535a42)   4  version          5  message type
 *   6  flags (bit 0: RX on)        7  channel          8  node id
 *   10 PAN id                      12 short address    14 TX power (dBm)
 *   15 RSSI (dBm)                  16 LQI              17 reserved
 *   18 payload length              20 extended address (8 bytes)
 * TX and RX carry a PSDU without FCS. STATUS carries a one or two byte status
 * record: CCA request, CCA response (busy flag) or TX result (collision flag).
 */

#ifndef ZEPHYR_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_MEDIUM_H_
#define ZEPHYR_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_MEDIUM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NSS_MEDIUM_VERSION         1U
#define NSS_MEDIUM_IEEE_ADDR_SIZE  8U
#define NSS_MEDIUM_MAX_PSDU_SIZE   127U
#define NSS_MEDIUM_HEADER_SIZE     28U
#define NSS_MEDIUM_MAX_PACKET_SIZE 192U
#define NSS_MEDIUM_BROADCAST_PAN   0xffffU
#define NSS_MEDIUM_BROADCAST_SHORT 0xffffU

enum nss_medium_msg_type {
	NSS_MEDIUM_MSG_HELLO = 1,
	NSS_MEDIUM_MSG_FILTER = 2,
	NSS_MEDIUM_MSG_TX = 3,
	NSS_MEDIUM_MSG_RX = 4,
	NSS_MEDIUM_MSG_STATUS = 5,
};

struct nss_medium_msg {
	enum nss_medium_msg_type type;
	uint16_t node_id;
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t channel;
	int8_t tx_power_dbm;
	int8_t rssi_dbm;
	uint8_t lqi;
	bool rx_on;
	uint8_t ieee_addr[NSS_MEDIUM_IEEE_ADDR_SIZE];
	const uint8_t *psdu;
	size_t psdu_len;
};

int nss_medium_encode(uint8_t *buffer, size_t capacity, const struct nss_medium_msg *msg,
		      size_t *encoded_len);
int nss_medium_decode(struct nss_medium_msg *msg, const uint8_t *buffer, size_t len);
int nss_medium_status_encode_cca_req(uint8_t *buffer, size_t capacity, size_t *encoded_len);
int nss_medium_status_decode_cca_rsp(const uint8_t *buffer, size_t len, bool *busy);
int nss_medium_status_decode_tx_result_rsp(const uint8_t *buffer, size_t len, bool *collision);

#endif /* ZEPHYR_DRIVERS_IEEE802154_NATIVE_SIM_SOCKET_MEDIUM_H_ */
