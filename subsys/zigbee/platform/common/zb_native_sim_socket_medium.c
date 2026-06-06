/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/zigbee/native_sim_socket_medium.h>

#include <errno.h>
#include <string.h>

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#define ZB_NATIVE_SIM_SOCKET_MEDIUM_MAGIC 0x4d535a42u
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_HEADER_SIZE 28U
#define ZB_NATIVE_SIM_SOCKET_MEDIUM_FLAG_RX_ON BIT(0)

static void zb_native_sim_socket_medium_put_le16(uint8_t *buf, uint16_t value)
{
	buf[0] = (uint8_t)value;
	buf[1] = (uint8_t)(value >> 8);
}

static void zb_native_sim_socket_medium_put_le32(uint8_t *buf, uint32_t value)
{
	buf[0] = (uint8_t)value;
	buf[1] = (uint8_t)(value >> 8);
	buf[2] = (uint8_t)(value >> 16);
	buf[3] = (uint8_t)(value >> 24);
}

static uint16_t zb_native_sim_socket_medium_get_le16(const uint8_t *buf)
{
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t zb_native_sim_socket_medium_get_le32(const uint8_t *buf)
{
	return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
	       ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

int zb_native_sim_socket_medium_encode(uint8_t *buffer, size_t capacity,
				       const struct zb_native_sim_socket_medium_msg *msg,
				       size_t *encoded_len)
{
	size_t total_len;

	if (buffer == NULL || msg == NULL || encoded_len == NULL) {
		return -EINVAL;
	}

	if (msg->psdu_len > ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PSDU_SIZE) {
		return -EMSGSIZE;
	}

	total_len = ZB_NATIVE_SIM_SOCKET_MEDIUM_HEADER_SIZE + msg->psdu_len;
	if (capacity < total_len) {
		return -ENOSPC;
	}

	zb_native_sim_socket_medium_put_le32(&buffer[0], ZB_NATIVE_SIM_SOCKET_MEDIUM_MAGIC);
	buffer[4] = ZB_NATIVE_SIM_SOCKET_MEDIUM_VERSION;
	buffer[5] = (uint8_t)msg->type;
	buffer[6] = msg->rx_on ? ZB_NATIVE_SIM_SOCKET_MEDIUM_FLAG_RX_ON : 0U;
	buffer[7] = msg->channel;
	zb_native_sim_socket_medium_put_le16(&buffer[8], msg->node_id);
	zb_native_sim_socket_medium_put_le16(&buffer[10], msg->pan_id);
	zb_native_sim_socket_medium_put_le16(&buffer[12], msg->short_addr);
	buffer[14] = (uint8_t)msg->tx_power_dbm;
	buffer[15] = (uint8_t)msg->rssi_dbm;
	buffer[16] = msg->lqi;
	buffer[17] = 0U;
	zb_native_sim_socket_medium_put_le16(&buffer[18], (uint16_t)msg->psdu_len);
	memset(&buffer[20], 0, ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE);
	memcpy(&buffer[20], msg->ieee_addr, ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE);

	if (msg->psdu_len != 0U && msg->psdu != NULL) {
		memcpy(&buffer[ZB_NATIVE_SIM_SOCKET_MEDIUM_HEADER_SIZE], msg->psdu, msg->psdu_len);
	}

	*encoded_len = total_len;
	return 0;
}

int zb_native_sim_socket_medium_decode(struct zb_native_sim_socket_medium_msg *msg,
				       const uint8_t *buffer, size_t len)
{
	size_t payload_len;

	if (msg == NULL || buffer == NULL || len < ZB_NATIVE_SIM_SOCKET_MEDIUM_HEADER_SIZE) {
		return -EINVAL;
	}

	if (zb_native_sim_socket_medium_get_le32(&buffer[0]) !=
	    ZB_NATIVE_SIM_SOCKET_MEDIUM_MAGIC) {
		return -EBADMSG;
	}

	if (buffer[4] != ZB_NATIVE_SIM_SOCKET_MEDIUM_VERSION) {
		return -EPROTONOSUPPORT;
	}

	payload_len = zb_native_sim_socket_medium_get_le16(&buffer[18]);
	if (payload_len > ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PSDU_SIZE ||
	    (ZB_NATIVE_SIM_SOCKET_MEDIUM_HEADER_SIZE + payload_len) > len) {
		return -EMSGSIZE;
	}

	memset(msg, 0, sizeof(*msg));
	msg->type = (enum zb_native_sim_socket_medium_msg_type)buffer[5];
	msg->rx_on = (buffer[6] & ZB_NATIVE_SIM_SOCKET_MEDIUM_FLAG_RX_ON) != 0U;
	msg->channel = buffer[7];
	msg->node_id = zb_native_sim_socket_medium_get_le16(&buffer[8]);
	msg->pan_id = zb_native_sim_socket_medium_get_le16(&buffer[10]);
	msg->short_addr = zb_native_sim_socket_medium_get_le16(&buffer[12]);
	msg->tx_power_dbm = (int8_t)buffer[14];
	msg->rssi_dbm = (int8_t)buffer[15];
	msg->lqi = buffer[16];
	memcpy(msg->ieee_addr, &buffer[20], ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE);
	msg->psdu = &buffer[ZB_NATIVE_SIM_SOCKET_MEDIUM_HEADER_SIZE];
	msg->psdu_len = payload_len;

	return 0;
}

void zb_native_sim_socket_medium_peer_reset(struct zb_native_sim_socket_medium_peer *peer)
{
	if (peer == NULL) {
		return;
	}

	memset(peer, 0, sizeof(*peer));
	peer->pan_id = ZB_NATIVE_SIM_SOCKET_MEDIUM_BROADCAST_PAN;
	peer->short_addr = ZB_NATIVE_SIM_SOCKET_MEDIUM_BROADCAST_SHORT;
}

int zb_native_sim_socket_medium_peer_apply(struct zb_native_sim_socket_medium_peer *peer,
					   const struct zb_native_sim_socket_medium_msg *msg)
{
	if (peer == NULL || msg == NULL) {
		return -EINVAL;
	}

	switch (msg->type) {
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO:
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER:
		peer->node_id = msg->node_id;
		peer->pan_id = msg->pan_id;
		peer->short_addr = msg->short_addr;
		peer->channel = msg->channel;
		peer->rx_on = msg->rx_on;
		memcpy(peer->ieee_addr, msg->ieee_addr,
		       ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE);
		return 0;
	default:
		return -ENOTSUP;
	}
}

bool zb_native_sim_socket_medium_peer_accepts_psdu(
	const struct zb_native_sim_socket_medium_peer *peer,
	const uint8_t *psdu, size_t psdu_len)
{
	uint16_t fcf;
	uint8_t dst_mode;
	uint16_t pan_id;
	size_t idx = 3U;

	if (peer == NULL || psdu == NULL || psdu_len < 7U || !peer->rx_on) {
		return false;
	}

	fcf = zb_native_sim_socket_medium_get_le16(psdu);
	dst_mode = (uint8_t)((fcf >> 10) & 0x3U);
	if (dst_mode == 0U) {
		return true;
	}

	if (psdu_len < (idx + 2U)) {
		return false;
	}

	pan_id = zb_native_sim_socket_medium_get_le16(&psdu[idx]);
	idx += 2U;
	if (peer->pan_id != ZB_NATIVE_SIM_SOCKET_MEDIUM_BROADCAST_PAN &&
	    pan_id != ZB_NATIVE_SIM_SOCKET_MEDIUM_BROADCAST_PAN &&
	    pan_id != peer->pan_id) {
		return false;
	}

	if (dst_mode == 0x02U) {
		uint16_t short_addr;

		if (psdu_len < (idx + 2U)) {
			return false;
		}

		short_addr = zb_native_sim_socket_medium_get_le16(&psdu[idx]);
		return short_addr == ZB_NATIVE_SIM_SOCKET_MEDIUM_BROADCAST_SHORT ||
		       short_addr == peer->short_addr;
	}

	if (dst_mode == 0x03U) {
		if (psdu_len < (idx + ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE)) {
			return false;
		}

		return memcmp(&psdu[idx], peer->ieee_addr,
			      ZB_NATIVE_SIM_SOCKET_MEDIUM_IEEE_ADDR_SIZE) == 0;
	}

	return false;
}
