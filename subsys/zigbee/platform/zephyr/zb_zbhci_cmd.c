/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ZBHCI narrow command subset for the coordinator role: BDB network
 * formation, Mgmt_Permit_Joining, child-node list, AF data send, and ZDO
 * Active_EP/Simple_Desc queries.
 *
 * Command IDs and wire-payload layouts are adapted from Telink's
 * tl_zigbee_sdk zbhci/zbhciCmdProcess.c (Copyright (c) 2021 Telink
 * Semiconductor (Shanghai) Co., Ltd., Apache-2.0); the handler bodies are
 * rewritten against this port's own ZDO/AF/BDB APIs (zbapi/zb_api.h,
 * af/zb_af.h, zephyr/zigbee/zb_bootstrap.h) rather than copied. Only the
 * commands above are implemented; anything else gets
 * ZBHCI_MSG_STATUS_UNHANDLED_COMMAND.
 */
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_zbhci.h>

#include "zb_common_stub.h"

LOG_MODULE_REGISTER(zigbee_zbhci_cmd, CONFIG_ZIGBEE_LOG_LEVEL);

static uint16_t read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | p[1];
}

static void write_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)(v & 0xffU);
}

static void zbhci_ack(uint16_t orig_cmd, enum zbhci_msg_status status)
{
	uint8_t payload[4];

	write_be16(payload, orig_cmd);
	payload[2] = (uint8_t)status;
	payload[3] = 0U;

	zb_zbhci_send(ZBHCI_CMD_ACKNOWLEDGE, sizeof(payload), payload);
}

static void zbhci_cmd_formation(uint16_t len, const uint8_t *payload)
{
	ARG_UNUSED(len);
	ARG_UNUSED(payload);

	(void)zb_platform_bdb_network_formation_start();
	zbhci_ack(ZBHCI_CMD_BDB_COMMISSION_FORMATION, ZBHCI_MSG_STATUS_SUCCESS);
}

static void zbhci_cmd_permit_join(uint16_t len, const uint8_t *payload)
{
	uint16_t target_addr;
	u8 seq = 0U;

	if (len < 4U) {
		zbhci_ack(ZBHCI_CMD_MGMT_PERMIT_JOIN_REQ, ZBHCI_MSG_STATUS_INCORRECT_PARAMETERS);
		return;
	}

	target_addr = read_be16(payload);
	(void)zb_mgmtPermitJoinReq(target_addr, payload[2], payload[3], &seq, NULL);
	zbhci_ack(ZBHCI_CMD_MGMT_PERMIT_JOIN_REQ, ZBHCI_MSG_STATUS_SUCCESS);
}

static void zbhci_cmd_get_child_nodes(uint16_t len, const uint8_t *payload)
{
	nwk_childTableInfo_t info;
	uint8_t out[sizeof(nwk_childNodesInfo_t) +
		    ZBHCI_CHILD_LIST_NUM_MAX * (EXT_ADDR_LEN + 2U)];
	uint8_t *o = out;
	uint8_t start_idx = (len >= 1U) ? payload[0] : 0U;
	uint8_t count;

	tl_childNodesListGet(start_idx, &info);

	*o++ = info.info.status;
	*o++ = info.info.totalChildNodesNum;
	*o++ = info.info.startIdx;
	*o++ = info.info.childNodesNum;

	count = info.info.childNodesNum;
	if (count > ZBHCI_CHILD_LIST_NUM_MAX) {
		count = ZBHCI_CHILD_LIST_NUM_MAX;
	}
	for (uint8_t i = 0; i < count; i++) {
		for (int b = EXT_ADDR_LEN - 1; b >= 0; b--) {
			*o++ = info.list[i].extAddr[b];
		}
		write_be16(o, info.list[i].nwkAddr);
		o += 2;
	}

	zb_zbhci_send(ZBHCI_CMD_GET_CHILD_NODES_RSP, (uint16_t)(o - out), out);
}

static void zbhci_cmd_af_data_send(uint16_t len, const uint8_t *payload)
{
	epInfo_t dst;
	uint16_t cluster_id;
	uint16_t data_len;
	u8 aps_cnt = 0U;
	u8 src_ep;

	if (len < 8U) {
		zbhci_ack(ZBHCI_CMD_AF_DATA_SEND_TEST_REQ, ZBHCI_MSG_STATUS_INCORRECT_PARAMETERS);
		return;
	}

	data_len = read_be16(&payload[6]);
	if (len < 8U + data_len) {
		zbhci_ack(ZBHCI_CMD_AF_DATA_SEND_TEST_REQ, ZBHCI_MSG_STATUS_INCORRECT_PARAMETERS);
		return;
	}

	memset(&dst, 0, sizeof(dst));
	dst.dstAddr.shortAddr = read_be16(payload);
	src_ep = payload[2];
	dst.dstEp = payload[3];
	dst.profileId = HA_PROFILE_ID;
	dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst.txOptions = APS_TX_OPT_ACK_TX;
	dst.radius = 30U;
	cluster_id = read_be16(&payload[4]);

	(void)af_dataSend(src_ep, &dst, cluster_id, data_len, (u8 *)&payload[8], &aps_cnt);
	zbhci_ack(ZBHCI_CMD_AF_DATA_SEND_TEST_REQ, ZBHCI_MSG_STATUS_SUCCESS);
}

static void zbhci_active_ep_rsp_cb(void *p)
{
	const zdo_zdpDataInd_t *ind = (const zdo_zdpDataInd_t *)p;
	const zdo_active_ep_resp_t *rsp;
	uint8_t out[8U + MAX_REQUESTED_CLUSTER_NUMBER];
	uint8_t *o = out;
	u8 ep_count;

	if (ind == NULL || ind->zpdu == NULL) {
		return;
	}
	rsp = (const zdo_active_ep_resp_t *)ind->zpdu;

	write_be16(o, ind->src_addr);
	o += 2;
	*o++ = rsp->seq_num;
	*o++ = rsp->status;
	write_be16(o, rsp->nwk_addr_interest);
	o += 2;

	ep_count = rsp->active_ep_count;
	if (ep_count > MAX_REQUESTED_CLUSTER_NUMBER) {
		ep_count = MAX_REQUESTED_CLUSTER_NUMBER;
	}
	*o++ = ep_count;
	for (u8 i = 0; i < ep_count; i++) {
		*o++ = rsp->active_ep_lst[i];
	}

	zb_zbhci_send(ZBHCI_CMD_DISCOVERY_ACTIVE_EP_RSP, (uint16_t)(o - out), out);
}

static void zbhci_simple_desc_rsp_cb(void *p)
{
	const zdo_zdpDataInd_t *ind = (const zdo_zdpDataInd_t *)p;
	const zdo_simple_descriptor_resp_t *rsp;
	uint8_t out[96];
	uint8_t *o = out;
	const uint8_t *list;
	u8 in_count;
	u8 out_count;

	if (ind == NULL || ind->zpdu == NULL) {
		return;
	}
	rsp = (const zdo_simple_descriptor_resp_t *)ind->zpdu;

	write_be16(o, ind->src_addr);
	o += 2;
	*o++ = rsp->seq_num;
	*o++ = rsp->status;
	write_be16(o, rsp->nwk_addr_interest);
	o += 2;
	*o++ = rsp->length;

	if (rsp->status == ZDO_SUCCESS && rsp->length > 0U) {
		*o++ = rsp->simple_descriptor.endpoint;
		write_be16(o, rsp->simple_descriptor.app_profile_id);
		o += 2;
		write_be16(o, rsp->simple_descriptor.app_dev_id);
		o += 2;
		*o++ = (uint8_t)((rsp->simple_descriptor.app_dev_ver << 4) |
				 rsp->simple_descriptor.reserved);

		list = rsp->simple_descriptor.listInfo;
		in_count = *list++;
		*o++ = in_count;
		for (u8 i = 0; i < in_count; i++) {
			write_be16(o, (uint16_t)(list[0] | ((uint16_t)list[1] << 8)));
			o += 2;
			list += 2;
		}

		out_count = *list++;
		*o++ = out_count;
		for (u8 i = 0; i < out_count; i++) {
			write_be16(o, (uint16_t)(list[0] | ((uint16_t)list[1] << 8)));
			o += 2;
			list += 2;
		}
	}

	zb_zbhci_send(ZBHCI_CMD_DISCOVERY_SIMPLE_DESC_RSP, (uint16_t)(o - out), out);
}

static void zbhci_cmd_active_ep_req(uint16_t len, const uint8_t *payload)
{
	zdo_active_ep_req_t req;
	uint16_t target_addr;
	u8 seq = 0U;

	if (len < 4U) {
		zbhci_ack(ZBHCI_CMD_DISCOVERY_ACTIVE_EP_REQ, ZBHCI_MSG_STATUS_INCORRECT_PARAMETERS);
		return;
	}

	target_addr = read_be16(payload);
	req.nwk_addr_interest = read_be16(&payload[2]);
	(void)zb_zdoActiveEpReq(target_addr, &req, &seq, zbhci_active_ep_rsp_cb);
}

static void zbhci_cmd_simple_desc_req(uint16_t len, const uint8_t *payload)
{
	zdo_simple_descriptor_req_t req;
	uint16_t target_addr;
	u8 seq = 0U;

	if (len < 5U) {
		zbhci_ack(ZBHCI_CMD_DISCOVERY_SIMPLE_DESC_REQ,
			  ZBHCI_MSG_STATUS_INCORRECT_PARAMETERS);
		return;
	}

	target_addr = read_be16(payload);
	req.nwk_addr_interest = read_be16(&payload[2]);
	req.endpoint = payload[4];
	(void)zb_zdoSimpleDescReq(target_addr, &req, &seq, zbhci_simple_desc_rsp_cb);
}

void zb_zbhci_cmd_handle(uint16_t msg_type, uint16_t len, const uint8_t *payload)
{
	switch (msg_type) {
	case ZBHCI_CMD_BDB_COMMISSION_FORMATION:
		zbhci_cmd_formation(len, payload);
		break;
	case ZBHCI_CMD_MGMT_PERMIT_JOIN_REQ:
		zbhci_cmd_permit_join(len, payload);
		break;
	case ZBHCI_CMD_GET_CHILD_NODES_REQ:
		zbhci_cmd_get_child_nodes(len, payload);
		break;
	case ZBHCI_CMD_AF_DATA_SEND_TEST_REQ:
		zbhci_cmd_af_data_send(len, payload);
		break;
	case ZBHCI_CMD_DISCOVERY_ACTIVE_EP_REQ:
		zbhci_cmd_active_ep_req(len, payload);
		break;
	case ZBHCI_CMD_DISCOVERY_SIMPLE_DESC_REQ:
		zbhci_cmd_simple_desc_req(len, payload);
		break;
	default:
		LOG_WRN("zbhci: unhandled command 0x%04x", msg_type);
		zbhci_ack(msg_type, ZBHCI_MSG_STATUS_UNHANDLED_COMMAND);
		break;
	}
}
