/* SPDX-License-Identifier: Apache-2.0 */

#include "coord_logic.h"

#include <string.h>

#define ZB_COORD_SHORT_ADDR 0x0000u
#define ZB_NO_SHORT_ADDR    0xffffu
#define ZB_DEVICE_IEEE      0xa4c138e050020002ULL
#define ZB_PROFILE_HA       0x0104u
#define ZB_PAN_ID           0x5b27u

#define ZB_MAC_FCF_COMMAND_SHORT 0x8863u
#define ZB_MAC_CMD_ASSOC_REQ     0x01u
#define ZB_MAC_CMD_ASSOC_RSP     0x02u
#define ZB_MAC_CMD_DATA_REQ      0x04u

#define ZB_ZDO_DEVICE_ANNCE    0x0013u
#define ZB_ZDO_ACTIVE_EP_REQ   0x0005u
#define ZB_ZDO_ACTIVE_EP_RSP   0x8005u
#define ZB_ZDO_SIMPLE_DESC_REQ 0x0004u
#define ZB_ZDO_SIMPLE_DESC_RSP 0x8004u
#define ZB_CLUSTER_BASIC       0x0000u
#define ZB_CLUSTER_NWK_MGMT    0xff00u
#define ZB_NWK_TIMEOUT_REQ     0x0bu
#define ZB_NWK_TIMEOUT_RSP     0x0cu
#define ZB_ZCL_CMD_READ        0x00u
#define ZB_ZCL_CMD_READ_RSP    0x01u
#define ZB_ZCL_ATTR_MODEL_ID   0x0005u
#define ZB_ZCL_TYPE_CHAR_STR   0x42u

static void put_le16(uint8_t *buf, uint16_t value)
{
	buf[0] = (uint8_t)value;
	buf[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *buf)
{
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void put_le64(uint8_t *buf, uint64_t value)
{
	for (size_t i = 0; i < 8; i++) {
		buf[i] = (uint8_t)(value >> (8 * i));
	}
}

static void queue_frame(struct zb_host_socket_coord *coord, enum zb_host_socket_frame_type type)
{
	if (coord->queued_count < sizeof(coord->queued_frames)) {
		coord->queued_frames[coord->queued_count++] = (uint8_t)type;
	}
}

static bool pop_frame(struct zb_host_socket_coord *coord, enum zb_host_socket_frame_type *type)
{
	if (coord->queued_count == 0U) {
		return false;
	}

	*type = (enum zb_host_socket_frame_type)coord->queued_frames[0];
	memmove(&coord->queued_frames[0], &coord->queued_frames[1], coord->queued_count - 1U);
	coord->queued_count--;
	return true;
}

static size_t encode_mac_command(uint8_t *wire, uint8_t seq, uint16_t dst, uint16_t src, uint8_t cmd)
{
	put_le16(&wire[0], ZB_MAC_FCF_COMMAND_SHORT);
	wire[2] = seq;
	put_le16(&wire[3], ZB_PAN_ID);
	put_le16(&wire[5], dst);
	put_le16(&wire[7], src);
	wire[9] = cmd;
	return 10U;
}

static size_t encode_data_frame(uint8_t *wire, uint8_t seq, uint16_t dst, uint16_t src,
				uint16_t cluster, uint8_t command,
				const uint8_t *payload, size_t payload_len)
{
	size_t idx = 15U;

	put_le16(&wire[0], 0x8861u);
	wire[2] = seq;
	put_le16(&wire[3], ZB_PAN_ID);
	put_le16(&wire[5], dst);
	put_le16(&wire[7], src);
	put_le16(&wire[9], ZB_PROFILE_HA);
	put_le16(&wire[11], cluster);
	wire[13] = 1U;
	wire[14] = 1U;
	wire[idx++] = 0x00U;
	wire[idx++] = seq;
	wire[idx++] = command;
	if (payload_len != 0U) {
		memcpy(&wire[idx], payload, payload_len);
		idx += payload_len;
	}

	return idx;
}

enum zb_host_socket_frame_type zb_host_socket_coord_identify_frame(const uint8_t *psdu, size_t psdu_len)
{
	uint16_t cluster;
	uint8_t command;
	size_t payload_len;

	if (psdu_len >= 10U && get_le16(&psdu[0]) == ZB_MAC_FCF_COMMAND_SHORT) {
		switch (psdu[9]) {
		case ZB_MAC_CMD_ASSOC_REQ:
			return ZB_HOST_SOCKET_FRAME_ASSOC_REQ;
		case ZB_MAC_CMD_ASSOC_RSP:
			return ZB_HOST_SOCKET_FRAME_ASSOC_RSP;
		case ZB_MAC_CMD_DATA_REQ:
			return ZB_HOST_SOCKET_FRAME_DATA_REQ;
		default:
			break;
		}
	}

	if (psdu_len < 18U || get_le16(&psdu[0]) != 0x8861u) {
		return ZB_HOST_SOCKET_FRAME_DATA_REQ;
	}

	cluster = get_le16(&psdu[11]);
	command = psdu[17];
	payload_len = psdu_len - 18U;

	switch (cluster) {
	case 0x0038u:
		return ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY;
	case ZB_CLUSTER_NWK_MGMT:
		return (command == ZB_NWK_TIMEOUT_REQ) ?
			ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ :
			ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP;
	case ZB_ZDO_DEVICE_ANNCE:
		return ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE;
	case ZB_ZDO_ACTIVE_EP_REQ:
		return ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ;
	case ZB_ZDO_ACTIVE_EP_RSP:
		return ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP;
	case ZB_ZDO_SIMPLE_DESC_REQ:
		return ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ;
	case ZB_ZDO_SIMPLE_DESC_RSP:
		return ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP;
	case ZB_CLUSTER_BASIC:
		if (command == ZB_ZCL_CMD_READ && payload_len >= 2U) {
			return ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ;
		}
		return ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP;
	default:
		return ZB_HOST_SOCKET_FRAME_DATA_REQ;
	}
}

static size_t encode_frame(enum zb_host_socket_frame_type type, uint8_t *wire,
			   uint16_t src, uint16_t dst, const char *model_id)
{
	uint8_t payload[48];
	size_t len = 0U;

	switch (type) {
	case ZB_HOST_SOCKET_FRAME_ASSOC_RSP:
		len = encode_mac_command(wire, 1U, dst, src, ZB_MAC_CMD_ASSOC_RSP);
		put_le16(&wire[10], dst);
		wire[12] = 0U;
		return 13U;
	case ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY:
		payload[0] = 0x05U;
		return encode_data_frame(wire, 2U, dst, src, 0x0038u, 0x05U, payload, 1U);
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP:
		payload[0] = ZB_NWK_TIMEOUT_RSP;
		payload[1] = 0x00U;
		payload[2] = 0x02U;
		return encode_data_frame(wire, 3U, dst, src, ZB_CLUSTER_NWK_MGMT,
					 ZB_NWK_TIMEOUT_RSP, payload, 3U);
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ:
		put_le16(&payload[0], dst);
		return encode_data_frame(wire, 4U, dst, src, ZB_ZDO_ACTIVE_EP_REQ, 0x00U,
					 payload, 2U);
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ:
		put_le16(&payload[0], dst);
		payload[2] = 1U;
		return encode_data_frame(wire, 5U, dst, src, ZB_ZDO_SIMPLE_DESC_REQ, 0x00U,
					 payload, 3U);
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ:
		put_le16(&payload[0], ZB_ZCL_ATTR_MODEL_ID);
		return encode_data_frame(wire, 6U, dst, src, ZB_CLUSTER_BASIC, ZB_ZCL_CMD_READ,
					 payload, 2U);
	case ZB_HOST_SOCKET_FRAME_ASSOC_REQ:
		len = encode_mac_command(wire, 1U, dst, src, ZB_MAC_CMD_ASSOC_REQ);
		put_le64(&wire[10], ZB_DEVICE_IEEE);
		return len + 8U;
	case ZB_HOST_SOCKET_FRAME_DATA_REQ:
		return encode_mac_command(wire, 2U, dst, src, ZB_MAC_CMD_DATA_REQ);
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ:
		payload[0] = ZB_NWK_TIMEOUT_REQ;
		payload[1] = 0x08U;
		return encode_data_frame(wire, 3U, dst, src, ZB_CLUSTER_NWK_MGMT,
					 ZB_NWK_TIMEOUT_REQ, payload, 2U);
	case ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE:
		put_le16(&payload[0], src);
		put_le64(&payload[2], ZB_DEVICE_IEEE);
		payload[10] = 0x80U;
		return encode_data_frame(wire, 4U, dst, src, ZB_ZDO_DEVICE_ANNCE, 0x00U,
					 payload, 11U);
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP:
		payload[0] = 0x00U;
		put_le16(&payload[1], src);
		payload[3] = 1U;
		payload[4] = 1U;
		return encode_data_frame(wire, 5U, dst, src, ZB_ZDO_ACTIVE_EP_RSP, 0x00U,
					 payload, 5U);
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP:
		payload[0] = 0x00U;
		put_le16(&payload[1], src);
		payload[3] = 8U;
		payload[4] = 1U;
		put_le16(&payload[5], ZB_PROFILE_HA);
		put_le16(&payload[7], 0x0100U);
		payload[9] = 0U;
		payload[10] = 1U;
		put_le16(&payload[11], ZB_CLUSTER_BASIC);
		return encode_data_frame(wire, 6U, dst, src, ZB_ZDO_SIMPLE_DESC_RSP, 0x00U,
					 payload, 13U);
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP: {
		size_t model_len = strlen(model_id != NULL ? model_id : "");

		put_le16(&payload[0], ZB_ZCL_ATTR_MODEL_ID);
		payload[2] = 0x00U;
		payload[3] = ZB_ZCL_TYPE_CHAR_STR;
		payload[4] = (uint8_t)model_len;
		memcpy(&payload[5], model_id, model_len);
		return encode_data_frame(wire, 7U, dst, src, ZB_CLUSTER_BASIC,
					 ZB_ZCL_CMD_READ_RSP, payload, model_len + 5U);
	}
	default:
		break;
	}

	return 0U;
}

struct zb_native_sim_socket_medium_msg zb_host_socket_coord_make_tx(
	uint16_t node_id, uint8_t channel, enum zb_host_socket_frame_type type,
	const char *model_id)
{
	static uint8_t wire[128];
	struct zb_native_sim_socket_medium_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX;
	msg.node_id = node_id;
	msg.channel = channel;
	msg.psdu = wire;
	msg.psdu_len = encode_frame(type, wire, 0x2700U, ZB_COORD_SHORT_ADDR, model_id);

	return msg;
}

void zb_host_socket_coord_init(struct zb_host_socket_coord *coord)
{
	memset(coord, 0, sizeof(*coord));
	coord->permit_join = true;
	coord->pan_id = ZB_PAN_ID;
	coord->next_child_short = 0x2700U;
	coord->child_short = ZB_NO_SHORT_ADDR;
	zb_native_sim_socket_medium_peer_reset(&coord->peer);
}

static void set_output(struct zb_host_socket_coord *coord,
		       struct zb_native_sim_socket_medium_msg *output,
		       enum zb_host_socket_frame_type type)
{
	memset(output, 0, sizeof(*output));
	output->type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX;
	output->node_id = coord->peer.node_id;
	output->channel = coord->peer.channel;
	output->rssi_dbm = -40;
	output->lqi = 255U;
	output->psdu = coord->output_psdu;
	output->psdu_len = encode_frame(type, coord->output_psdu, ZB_COORD_SHORT_ADDR,
					 coord->child_short, NULL);
	coord->output_psdu_len = output->psdu_len;
}

static void maybe_capture_model_id(struct zb_host_socket_coord *coord,
				   const struct zb_native_sim_socket_medium_msg *input)
{
	uint8_t str_len;

	if (input->psdu_len < 23U || get_le16(&input->psdu[18]) != ZB_ZCL_ATTR_MODEL_ID) {
		return;
	}

	str_len = input->psdu[22];
	if ((size_t)(23U + str_len) > input->psdu_len || str_len >= sizeof(coord->observed_model_id)) {
		return;
	}

	memcpy(coord->observed_model_id, &input->psdu[23], str_len);
	coord->observed_model_id[str_len] = '\0';
	coord->interview_complete = true;
}

int zb_host_socket_coord_process(struct zb_host_socket_coord *coord,
				 const struct zb_native_sim_socket_medium_msg *input,
				 struct zb_native_sim_socket_medium_msg *output)
{
	enum zb_host_socket_frame_type type;
	enum zb_host_socket_frame_type queued;

	if (input == NULL) {
		return 0;
	}

	if (input->type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER ||
	    input->type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO) {
		(void)zb_native_sim_socket_medium_peer_apply(&coord->peer, input);
		return 0;
	}

	type = zb_host_socket_coord_identify_frame(input->psdu, input->psdu_len);

	switch (type) {
	case ZB_HOST_SOCKET_FRAME_ASSOC_REQ:
		coord->last_assoc_status = coord->permit_join ? 0U : 1U;
		if (coord->permit_join) {
			coord->child_short = coord->next_child_short++;
			queue_frame(coord, ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);
		}
		if (output != NULL) {
			memset(output, 0, sizeof(*output));
			output->type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX;
			output->node_id = coord->peer.node_id;
			output->channel = coord->peer.channel;
			output->rssi_dbm = -40;
			output->lqi = 255U;
			output->psdu = coord->output_psdu;
			output->psdu_len = encode_frame(ZB_HOST_SOCKET_FRAME_ASSOC_RSP,
							coord->output_psdu,
							ZB_COORD_SHORT_ADDR,
							coord->permit_join ? coord->child_short :
									ZB_NO_SHORT_ADDR,
							NULL);
			coord->output_psdu[12] = coord->last_assoc_status;
			return 1;
		}
		return 0;
	case ZB_HOST_SOCKET_FRAME_DATA_REQ:
		if (output != NULL && pop_frame(coord, &queued)) {
			set_output(coord, output, queued);
			return 1;
		}
		return 0;
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ:
		coord->got_timeout_req = true;
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP);
		return 0;
	case ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE:
		coord->got_device_announce = true;
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ);
		return 0;
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP:
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ);
		return 0;
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP:
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ);
		return 0;
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP:
		maybe_capture_model_id(coord, input);
		return 0;
	default:
		return 0;
	}
}

void zb_host_socket_coord_observed_model_id(const struct zb_host_socket_coord *coord,
					    char *buffer, size_t buffer_len)
{
	if (buffer_len == 0U) {
		return;
	}

	strncpy(buffer, coord->observed_model_id, buffer_len - 1U);
	buffer[buffer_len - 1U] = '\0';
}

uint8_t zb_host_socket_coord_last_assoc_status(const struct zb_host_socket_coord *coord)
{
	return coord->last_assoc_status;
}
