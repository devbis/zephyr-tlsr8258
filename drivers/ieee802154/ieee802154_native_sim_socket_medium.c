/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ieee802154_native_sim_socket_medium.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define NSS_MEDIUM_MAGIC               0x4d535a42U
#define NSS_MEDIUM_FLAG_RX_ON          BIT(0)
#define NSS_MEDIUM_STATUS_CCA_REQ      0x01U
#define NSS_MEDIUM_STATUS_CCA_RSP      0x02U
#define NSS_MEDIUM_STATUS_TX_RESULT    0x03U
#define NSS_MEDIUM_STATUS_CCA_REQ_LEN  1U
#define NSS_MEDIUM_STATUS_FLAG_RSP_LEN 2U

int nss_medium_encode(uint8_t *buffer, size_t capacity, const struct nss_medium_msg *msg,
		      size_t *encoded_len)
{
	size_t total_len;

	if ((buffer == NULL) || (msg == NULL) || (encoded_len == NULL)) {
		return -EINVAL;
	}
	if (msg->psdu_len > NSS_MEDIUM_MAX_PSDU_SIZE) {
		return -EMSGSIZE;
	}

	total_len = NSS_MEDIUM_HEADER_SIZE + msg->psdu_len;
	if (capacity < total_len) {
		return -ENOSPC;
	}

	sys_put_le32(NSS_MEDIUM_MAGIC, &buffer[0]);
	buffer[4] = NSS_MEDIUM_VERSION;
	buffer[5] = (uint8_t)msg->type;
	buffer[6] = msg->rx_on ? NSS_MEDIUM_FLAG_RX_ON : 0U;
	buffer[7] = msg->channel;
	sys_put_le16(msg->node_id, &buffer[8]);
	sys_put_le16(msg->pan_id, &buffer[10]);
	sys_put_le16(msg->short_addr, &buffer[12]);
	buffer[14] = (uint8_t)msg->tx_power_dbm;
	buffer[15] = (uint8_t)msg->rssi_dbm;
	buffer[16] = msg->lqi;
	buffer[17] = 0U;
	sys_put_le16((uint16_t)msg->psdu_len, &buffer[18]);
	memcpy(&buffer[20], msg->ieee_addr, NSS_MEDIUM_IEEE_ADDR_SIZE);

	if ((msg->psdu_len != 0U) && (msg->psdu != NULL)) {
		memcpy(&buffer[NSS_MEDIUM_HEADER_SIZE], msg->psdu, msg->psdu_len);
	}

	*encoded_len = total_len;
	return 0;
}

int nss_medium_decode(struct nss_medium_msg *msg, const uint8_t *buffer, size_t len)
{
	size_t payload_len;

	if ((msg == NULL) || (buffer == NULL) || (len < NSS_MEDIUM_HEADER_SIZE)) {
		return -EINVAL;
	}
	if (sys_get_le32(&buffer[0]) != NSS_MEDIUM_MAGIC) {
		return -EBADMSG;
	}
	if (buffer[4] != NSS_MEDIUM_VERSION) {
		return -EPROTONOSUPPORT;
	}

	payload_len = sys_get_le16(&buffer[18]);
	if ((payload_len > NSS_MEDIUM_MAX_PSDU_SIZE) ||
	    ((NSS_MEDIUM_HEADER_SIZE + payload_len) > len)) {
		return -EMSGSIZE;
	}

	memset(msg, 0, sizeof(*msg));
	msg->type = (enum nss_medium_msg_type)buffer[5];
	msg->rx_on = (buffer[6] & NSS_MEDIUM_FLAG_RX_ON) != 0U;
	msg->channel = buffer[7];
	msg->node_id = sys_get_le16(&buffer[8]);
	msg->pan_id = sys_get_le16(&buffer[10]);
	msg->short_addr = sys_get_le16(&buffer[12]);
	msg->tx_power_dbm = (int8_t)buffer[14];
	msg->rssi_dbm = (int8_t)buffer[15];
	msg->lqi = buffer[16];
	memcpy(msg->ieee_addr, &buffer[20], NSS_MEDIUM_IEEE_ADDR_SIZE);
	msg->psdu = &buffer[NSS_MEDIUM_HEADER_SIZE];
	msg->psdu_len = payload_len;

	return 0;
}

int nss_medium_status_encode_cca_req(uint8_t *buffer, size_t capacity, size_t *encoded_len)
{
	if ((buffer == NULL) || (encoded_len == NULL)) {
		return -EINVAL;
	}
	if (capacity < NSS_MEDIUM_STATUS_CCA_REQ_LEN) {
		return -ENOSPC;
	}

	buffer[0] = NSS_MEDIUM_STATUS_CCA_REQ;
	*encoded_len = NSS_MEDIUM_STATUS_CCA_REQ_LEN;
	return 0;
}

static int nss_medium_status_decode_flag(const uint8_t *buffer, size_t len, uint8_t type,
					 bool *flag)
{
	if ((buffer == NULL) || (flag == NULL)) {
		return -EINVAL;
	}
	if ((len != NSS_MEDIUM_STATUS_FLAG_RSP_LEN) || (buffer[0] != type) || (buffer[1] > 1U)) {
		return -EBADMSG;
	}

	*flag = buffer[1] != 0U;
	return 0;
}

int nss_medium_status_decode_cca_rsp(const uint8_t *buffer, size_t len, bool *busy)
{
	return nss_medium_status_decode_flag(buffer, len, NSS_MEDIUM_STATUS_CCA_RSP, busy);
}

int nss_medium_status_decode_tx_result_rsp(const uint8_t *buffer, size_t len, bool *collision)
{
	return nss_medium_status_decode_flag(buffer, len, NSS_MEDIUM_STATUS_TX_RESULT, collision);
}
