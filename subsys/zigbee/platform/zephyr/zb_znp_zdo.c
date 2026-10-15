/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ZNP ZDO commands. A ZDO request is turned into the over-the-air ZDO frame
 * and sent from endpoint 0. The matching response is not parsed: the payload
 * of a ZNP *_RSP message is the sender's address followed by the over-the-air
 * response payload, so every response the node receives is forwarded to the
 * host as the message its cluster maps to. Hosts that registered for raw ZDO
 * callbacks also get each frame as ZDO_MSG_CB_INCOMING.
 */
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "zb_common.h"
#include "aps/aps_api.h"
#include "zb_znp_internal.h"

LOG_MODULE_DECLARE(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZNP_ZDO_MSG_CB_INCOMING   0xFFU
#define ZNP_ZDO_END_DEVICE_ANNCE  0xC1U
#define ZNP_ZDO_LEAVE_IND         0xC9U
#define ZNP_ZDO_TC_DEV_IND        0xCAU
#define ZNP_ZDO_PERMIT_JOIN_IND   0xCBU
#define ZNP_ZDO_MGMT_PERMIT_RSP   0xB6U

#define ZDO_CLUSTER_DEVICE_ANNCE  0x0013U
#define ZDO_CLUSTER_RSP_BIT       0x8000U

#define NWK_ADDR_UNKNOWN          0xFFFEU
#define NWK_ADDR_BROADCAST_ALL    0xFFFDU

static bool raw_callbacks;
static uint8_t zdo_tsn;

/* Send one ZDO request frame: TSN followed by @p payload. */
static uint8_t zdo_send(uint16_t dst, uint16_t cluster, const uint8_t *payload, uint8_t len)
{
	uint8_t frame[ZB_BUF_SIZE];
	epInfo_t ep;
	u8 aps_cnt = 0U;

	if (len + 1U > sizeof(frame)) {
		return ZNP_STATUS_INVALID_PARAMETER;
	}

	frame[0] = ++zdo_tsn;
	memcpy(&frame[1], payload, len);

	memset(&ep, 0, sizeof(ep));
	ep.dstAddr.shortAddr = dst;
	ep.dstEp = ZDO_EP;
	ep.profileId = ZDO_PROFILE_ID;
	ep.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	ep.txOptions = APS_TX_OPT_ACK_TX;
	ep.radius = 30U;

	return (af_dataSend(ZDO_EP, &ep, cluster, (u16)(len + 1U), frame, &aps_cnt) ==
		APS_STATUS_SUCCESS)
		       ? ZNP_STATUS_SUCCESS
		       : ZNP_STATUS_FAILURE;
}

/*
 * A request whose MT fields after the destination address are the over-the-air
 * payload, byte for byte.
 */
struct zdo_passthrough {
	uint8_t cmd;
	uint16_t cluster;
	uint8_t min_len; /* including the 2-byte destination address */
};

static const struct zdo_passthrough passthrough[] = {
	{0x01U, 0x0001U, 6U}, /* IEEE_ADDR_REQ: dst, nwk addr, request type, start index */
	{0x02U, 0x0002U, 4U}, /* NODE_DESC_REQ */
	{0x03U, 0x0003U, 4U}, /* POWER_DESC_REQ */
	{0x04U, 0x0004U, 5U}, /* SIMPLE_DESC_REQ */
	{0x05U, 0x0005U, 4U}, /* ACTIVE_EP_REQ */
	{0x06U, 0x0006U, 7U}, /* MATCH_DESC_REQ */
	{0x31U, 0x0031U, 3U}, /* MGMT_LQI_REQ */
	{0x32U, 0x0032U, 3U}, /* MGMT_RTG_REQ */
	{0x33U, 0x0033U, 3U}, /* MGMT_BIND_REQ */
	{0x34U, 0x0034U, 11U}, /* MGMT_LEAVE_REQ: dst, device address, flags */
};

static void zdo_passthrough_req(uint8_t cmd, uint16_t cluster, const uint8_t *d, uint8_t len)
{
	znp_srsp_status(ZNP_SUBSYS_ZDO, cmd, zdo_send(znp_get_u16(d), cluster, &d[2], len - 2U));
}

static void zdo_nwk_addr_req(const uint8_t *d, uint8_t len)
{
	/* The MT request has no destination; Z-Stack broadcasts it. */
	if (len < 10U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x00U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x00U, zdo_send(NWK_ADDR_BROADCAST_ALL, 0x0000U, d, len));
}

static void zdo_bind_req(uint8_t cmd, uint16_t cluster, const uint8_t *d, uint8_t len)
{
	uint8_t pl[8 + 1 + 2 + 1 + 8 + 1];
	uint8_t *p = pl;

	/* dst, src ieee, src ep, cluster, dst mode, dst address (8 bytes), dst ep */
	if (len < 23U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, cmd, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	memcpy(p, &d[2], 8U + 1U + 2U + 1U);
	p += 12;
	if (d[13] == 0x01U) { /* group address: two bytes, no endpoint */
		memcpy(p, &d[14], 2U);
		p += 2;
	} else {
		memcpy(p, &d[14], 8U);
		p += 8;
		*p++ = d[22];
	}

	znp_srsp_status(ZNP_SUBSYS_ZDO, cmd, zdo_send(znp_get_u16(d), cluster, pl, (uint8_t)(p - pl)));
}

static void zdo_bind(const uint8_t *d, uint8_t len)
{
	zdo_bind_req(0x21U, 0x0021U, d, len);
}

static void zdo_unbind(const uint8_t *d, uint8_t len)
{
	zdo_bind_req(0x22U, 0x0022U, d, len);
}

static void zdo_msg_cb_register(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	raw_callbacks = true;
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x3EU, ZNP_STATUS_SUCCESS);
}

static void permit_join_rsp(uint16_t src, uint8_t status)
{
	uint8_t d[3];

	(void)znp_put_u16(d, src);
	d[2] = status;
	znp_areq(ZNP_SUBSYS_ZDO, ZNP_ZDO_MGMT_PERMIT_RSP, d, sizeof(d));
}

static void permit_join_remote_cb(void *p)
{
	const zdo_zdpDataInd_t *ind = (const zdo_zdpDataInd_t *)p;

	if (ind != NULL && ind->zpdu != NULL) {
		permit_join_rsp(ind->src_addr, ind->zpdu[1]);
	}
}

static void zdo_mgmt_permit_join_req(const uint8_t *d, uint8_t len)
{
	uint16_t dst;
	bool local;
	u8 seq = 0U;
	zdo_status_t status;

	if (len < 5U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x36U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	/* d[0] is the address mode: a short address or a broadcast. */
	dst = znp_get_u16(&d[1]);
	local = (dst == zb_getLocalShortAddr()) || ZB_NWK_IS_ADDRESS_BROADCAST(dst);

	status = zb_mgmtPermitJoinReq(dst, d[3], d[4], &seq, local ? NULL : permit_join_remote_cb);
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x36U,
			(status == ZDO_SUCCESS) ? ZNP_STATUS_SUCCESS : ZNP_STATUS_FAILURE);

	if (local && status == ZDO_SUCCESS) {
		uint8_t duration = d[3];

		znp_areq(ZNP_SUBSYS_ZDO, ZNP_ZDO_PERMIT_JOIN_IND, &duration, 1U);
	}
	if (local) {
		permit_join_rsp(zb_getLocalShortAddr(),
				(status == ZDO_SUCCESS) ? ZNP_STATUS_SUCCESS : ZNP_STATUS_FAILURE);
	}
}

static void zdo_ext_route_disc(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	/* Routes are discovered by the stack when a frame needs one. */
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x45U, ZNP_STATUS_SUCCESS);
}

static void zdo_ext_route_chk(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x46U, ZNP_STATUS_SUCCESS);
}

static void zdo_mgmt_nwk_update_req(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	/* Moving the network to another channel is not supported. */
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U, ZNP_STATUS_FAILURE);
}

static void zdo_ext_nwk_info(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[2 + 1 + 2 + 2 + 8 + 8 + 1];
	uint8_t *p = rsp;

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	p = znp_put_u16(p, zb_getLocalShortAddr());
	*p++ = znp_device_state();
	p = znp_put_u16(p, g_zbNIB.panId);
	p = znp_put_u16(p, 0xFFFEU); /* the coordinator has no parent */
	memcpy(p, g_zbNIB.extPANId, 8U);
	p += 8;
	memset(p, 0, 8U);
	p += 8;
	*p++ = g_zbMacPib.phyChannelCur;
	znp_srsp(ZNP_SUBSYS_ZDO, 0x50U, rsp, sizeof(rsp));
}

/* The local group table: endpoint, group ID, name length, name. */

static void zdo_ext_add_group(const uint8_t *d, uint8_t len)
{
	aps_add_group_req_t req;

	if (len < 4U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x4BU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	req.ep = d[0];
	req.group_addr = znp_get_u16(&d[1]);
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x4BU,
			(aps_me_group_add_req(&req) == APS_STATUS_SUCCESS) ? ZNP_STATUS_SUCCESS
									   : ZNP_STATUS_FAILURE);
}

static void zdo_ext_find_group(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[4];

	if (len < 3U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x4AU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	rsp[0] = (aps_group_search(znp_get_u16(&d[1]), d[0]) != NULL) ? ZNP_STATUS_SUCCESS
								     : ZNP_STATUS_FAILURE;
	(void)znp_put_u16(&rsp[1], znp_get_u16(&d[1]));
	rsp[3] = 0U; /* group names are not kept */
	znp_srsp(ZNP_SUBSYS_ZDO, 0x4AU, rsp, sizeof(rsp));
}

static void zdo_ext_remove_group(const uint8_t *d, uint8_t len)
{
	aps_delete_group_req_t req;

	if (len < 3U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x47U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	req.ep = d[0];
	req.group_addr = znp_get_u16(&d[1]);
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x47U,
			(aps_me_group_delete_req(&req) == APS_STATUS_SUCCESS) ? ZNP_STATUS_SUCCESS
									      : ZNP_STATUS_FAILURE);
}

static void zdo_ext_remove_all_groups(const uint8_t *d, uint8_t len)
{
	if (len < 1U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x48U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x48U,
			(aps_me_group_delete_all_req(d[0]) == APS_STATUS_SUCCESS)
				? ZNP_STATUS_SUCCESS
				: ZNP_STATUS_FAILURE);
}

static void zdo_ext_count_all_groups(const uint8_t *d, uint8_t len)
{
	ARG_UNUSED(d);
	ARG_UNUSED(len);

	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x4CU, aps_group_entry_num_get());
}

static void zdo_startup_from_app(const uint8_t *d, uint8_t len)
{
	uint8_t rsp;

	ARG_UNUSED(d);
	ARG_UNUSED(len);

	/* The response goes first: the state change it leads to follows as an indication. */
	if (g_zbNwkCtx.joined) {
		rsp = ZNP_NETWORK_RESTORED;
	} else {
		rsp = ZNP_NETWORK_NEW;
	}
	znp_srsp(ZNP_SUBSYS_ZDO, 0x40U, &rsp, 1U);

	if (znp_network_start(0x04U) == ZNP_NETWORK_NOT_STARTED) {
		LOG_WRN("znp: startup from the host failed");
	}
}

static const struct znp_cmd zdo_cmds[] = {
	{ZNP_SUBSYS_ZDO, 0x00U, zdo_nwk_addr_req},
	{ZNP_SUBSYS_ZDO, 0x21U, zdo_bind},
	{ZNP_SUBSYS_ZDO, 0x22U, zdo_unbind},
	{ZNP_SUBSYS_ZDO, 0x36U, zdo_mgmt_permit_join_req},
	{ZNP_SUBSYS_ZDO, 0x37U, zdo_mgmt_nwk_update_req},
	{ZNP_SUBSYS_ZDO, 0x3EU, zdo_msg_cb_register},
	{ZNP_SUBSYS_ZDO, 0x40U, zdo_startup_from_app},
	{ZNP_SUBSYS_ZDO, 0x45U, zdo_ext_route_disc},
	{ZNP_SUBSYS_ZDO, 0x46U, zdo_ext_route_chk},
	{ZNP_SUBSYS_ZDO, 0x47U, zdo_ext_remove_group},
	{ZNP_SUBSYS_ZDO, 0x48U, zdo_ext_remove_all_groups},
	{ZNP_SUBSYS_ZDO, 0x4AU, zdo_ext_find_group},
	{ZNP_SUBSYS_ZDO, 0x4BU, zdo_ext_add_group},
	{ZNP_SUBSYS_ZDO, 0x4CU, zdo_ext_count_all_groups},
	{ZNP_SUBSYS_ZDO, 0x50U, zdo_ext_nwk_info},
};

bool znp_zdo_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len)
{
	if (subsys != ZNP_SUBSYS_ZDO) {
		return false;
	}

	for (size_t i = 0; i < ARRAY_SIZE(passthrough); i++) {
		if (passthrough[i].cmd == cmd) {
			if (len < passthrough[i].min_len) {
				znp_srsp_status(ZNP_SUBSYS_ZDO, cmd, ZNP_STATUS_INVALID_PARAMETER);
			} else {
				zdo_passthrough_req(cmd, passthrough[i].cluster, data, len);
			}
			return true;
		}
	}

	return znp_cmd_dispatch(zdo_cmds, ARRAY_SIZE(zdo_cmds), subsys, cmd, data, len);
}

/* Received ZDO frames. */

void zb_platform_app_zdo_rx(uint16_t src_addr, bool broadcast, uint16_t cluster_id,
			    const uint8_t *payload, uint16_t len)
{
	uint8_t out[ZNP_MAX_DATA];
	uint8_t *p = out;

	if (len < 1U || len > ZNP_MAX_DATA - 9U) {
		return;
	}

	if (raw_callbacks) {
		p = znp_put_u16(p, src_addr);
		*p++ = broadcast ? 1U : 0U;
		p = znp_put_u16(p, cluster_id);
		*p++ = 0U;
		*p++ = payload[0];
		p = znp_put_u16(p, zb_getLocalShortAddr());
		memcpy(p, &payload[1], len - 1U);
		p += len - 1U;
		znp_areq(ZNP_SUBSYS_ZDO, ZNP_ZDO_MSG_CB_INCOMING, out, (size_t)(p - out));
	}

	if ((cluster_id & ZDO_CLUSTER_RSP_BIT) != 0U) {
		/* The *_RSP message: sender, then the response payload without its sequence number. */
		p = znp_put_u16(out, src_addr);
		memcpy(p, &payload[1], len - 1U);
		znp_areq(ZNP_SUBSYS_ZDO, (uint8_t)(0x80U + (cluster_id & 0xffU)), out,
			 2U + len - 1U);
	} else if (cluster_id == ZDO_CLUSTER_DEVICE_ANNCE && len >= 12U) {
		p = znp_put_u16(out, src_addr);
		memcpy(p, &payload[1], 11U); /* nwk address, IEEE address, capabilities */
		znp_areq(ZNP_SUBSYS_ZDO, ZNP_ZDO_END_DEVICE_ANNCE, out, 2U + 11U);
	}
}

void zb_platform_app_device_joined(uint16_t nwk_addr, const uint8_t ieee_addr[8],
				   const uint8_t parent_ieee_addr[8])
{
	uint8_t d[2 + 8 + 2];
	uint8_t local[8];
	uint16_t parent = NWK_ADDR_UNKNOWN;

	/* A device that joined through the trust center itself has no other parent. */
	zb_getLocalExtAddr(local);
	if (memcmp(parent_ieee_addr, local, 8U) == 0 || ZB_IEEE_ADDR_IS_ZERO(parent_ieee_addr) ||
	    zb_getNwkAddrByExtAddr((u8 *)parent_ieee_addr, &parent) != 0U) {
		parent = zb_getLocalShortAddr();
	}

	(void)znp_put_u16(d, nwk_addr);
	memcpy(&d[2], ieee_addr, 8U);
	(void)znp_put_u16(&d[10], parent);
	znp_areq(ZNP_SUBSYS_ZDO, ZNP_ZDO_TC_DEV_IND, d, sizeof(d));
}

void zb_platform_app_device_left(const uint8_t ieee_addr[8], bool rejoin)
{
	uint8_t d[2 + 8 + 3];
	uint16_t src = NWK_ADDR_UNKNOWN;

	(void)zb_getNwkAddrByExtAddr((u8 *)ieee_addr, &src);

	(void)znp_put_u16(d, src);
	memcpy(&d[2], ieee_addr, 8U);
	d[10] = 0U; /* an indication, not a request */
	d[11] = 0U;
	d[12] = rejoin ? 1U : 0U;
	znp_areq(ZNP_SUBSYS_ZDO, ZNP_ZDO_LEAVE_IND, d, sizeof(d));
}
