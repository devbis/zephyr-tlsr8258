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
#include "ev_timer.h"
#include "mac/mac.h"
#include "nwk/nwk_routing.h"
#include "nwk/includes/nwk_neighbor.h"
#include "nwk/nwk_data.h"
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

/* The clusters the host asked to see raw; 0xFFFF registers all of them. */
#define ZDO_CB_CLUSTERS 16U
#define ZDO_CB_ALL      0xFFFFU

static uint16_t cb_clusters[ZDO_CB_CLUSTERS];
static uint8_t cb_count;
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
	{0x07U, 0x0010U, 4U}, /* COMPLEX_DESC_REQ */
	{0x08U, 0x0011U, 4U}, /* USER_DESC_REQ */
	{0x0BU, 0x0014U, 5U}, /* USER_DESC_SET: dst, nwk addr, length, descriptor */
	{0x30U, 0x0030U, 8U}, /* MGMT_NWK_DISC_REQ: dst, channels, duration, start index */
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

static bool cb_wanted(uint16_t cluster)
{
	for (uint8_t i = 0U; i < cb_count; i++) {
		if (cb_clusters[i] == ZDO_CB_ALL || cb_clusters[i] == cluster) {
			return true;
		}
	}

	return false;
}

static void zdo_msg_cb_register(const uint8_t *d, uint8_t len)
{
	uint16_t cluster;

	if (len < 2U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x3EU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	cluster = znp_get_u16(d);
	if (!cb_wanted(cluster)) {
		if (cb_count == ZDO_CB_CLUSTERS) {
			znp_srsp_status(ZNP_SUBSYS_ZDO, 0x3EU, ZNP_STATUS_MEM_ERROR);
			return;
		}
		cb_clusters[cb_count++] = cluster;
	}

	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x3EU, ZNP_STATUS_SUCCESS);
}

static void zdo_msg_cb_remove(const uint8_t *d, uint8_t len)
{
	uint16_t cluster;

	if (len < 2U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x3FU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	cluster = znp_get_u16(d);
	if (cluster == ZDO_CB_ALL) {
		cb_count = 0U;
	}
	for (uint8_t i = 0U; i < cb_count; i++) {
		if (cb_clusters[i] == cluster) {
			cb_clusters[i] = cb_clusters[--cb_count];
			break;
		}
	}

	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x3FU, ZNP_STATUS_SUCCESS);
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

/* The host is told when a timed permit join window closes. */
static struct ev_timer_event_t *permit_timer;

static int permit_close_tick(void *arg)
{
	uint8_t closed = 0U;

	ARG_UNUSED(arg);

	permit_timer = NULL;
	znp_areq(ZNP_SUBSYS_ZDO, ZNP_ZDO_PERMIT_JOIN_IND, &closed, 1U);
	return -1;
}

static void permit_close_arm(uint8_t duration)
{
	if (permit_timer != NULL) {
		(void)ev_timer_taskCancel(&permit_timer);
		permit_timer = NULL;
	}

	/* No window, or one that never closes, has nothing to announce. */
	if (duration != 0U && duration != 0xffU) {
		permit_timer = ev_timer_taskPost(permit_close_tick, NULL,
						 (uint32_t)duration * 1000U);
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
		permit_close_arm(duration);
	}
	if (local) {
		permit_join_rsp(zb_getLocalShortAddr(),
				(status == ZDO_SUCCESS) ? ZNP_STATUS_SUCCESS : ZNP_STATUS_FAILURE);
	}
}

/*
 * Routes. A route exists when the stack has an active routing entry, or the
 * destination is a neighbor it can reach directly.
 */
#define ZNP_ROUTING_SUCCESS       0U
#define ZNP_ROUTING_FAIL          1U
#define ZNP_ROUTING_NO_ENTRY      4U
#define ZNP_ROUTING_INVALID_PARAM 6U

#define ROUTE_DISC_MTO_NO_CACHE   0x02U
#define ROUTE_DISC_MTO            0x01U

static void zdo_ext_route_chk(const uint8_t *d, uint8_t len)
{
	uint16_t dst;

	if (len < 2U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x46U, ZNP_ROUTING_INVALID_PARAM);
		return;
	}

	dst = znp_get_u16(d);
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x46U,
			(dst == zb_getLocalShortAddr() ||
			 nwkRoutingTabEntryDstActiveGet(dst) != NULL ||
			 nwkValidNeighborToFwd(dst) != NULL)
				? ZNP_ROUTING_SUCCESS
				: ZNP_ROUTING_NO_ENTRY);
}

static uint8_t route_discovery(uint16_t dst, bool many_to_one, bool no_cache, uint8_t radius)
{
	nlme_routeDisc_req_t *req;
	zb_buf_t *buf = zb_buf_allocate();

	if (buf == NULL) {
		return ZNP_ROUTING_FAIL;
	}

	req = (nlme_routeDisc_req_t *)buf;
	memset(req, 0, sizeof(*req));
	req->dstAddr = dst;
	req->dstAddrMode = many_to_one ? ADDR_MODE_NONE : ZB_ADDR_16BIT_DEV_OR_BROADCAST;
	req->radius = radius;
	req->noRouteCache = no_cache;
	buf->hdr.used = 1U;

	if (tl_zbPrimitivePost(TL_Q_HIGH2NWK, NWK_NLME_ROUTE_DISCOVERY_REQ, buf) != RET_OK) {
		zb_buf_free(buf);
		return ZNP_ROUTING_FAIL;
	}

	return ZNP_ROUTING_SUCCESS;
}

static void zdo_ext_route_disc(const uint8_t *d, uint8_t len)
{
	uint8_t options;

	if (len < 4U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x45U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	options = d[2];
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x45U,
			route_discovery(znp_get_u16(d), (options & ROUTE_DISC_MTO) != 0U,
					(options & ROUTE_DISC_MTO_NO_CACHE) != 0U, d[3]) ==
					ZNP_ROUTING_SUCCESS
				? ZNP_STATUS_SUCCESS
				: ZNP_STATUS_FAILURE);
}

/*
 * The coordinator, as a concentrator, announces itself with a many-to-one
 * route request now and then, so that routers keep a route back to it. The
 * host sets the period, and whether to do it at all, in NV.
 */
static uint32_t concentrator_period_ms;
static struct ev_timer_event_t *concentrator_timer;

static int concentrator_tick(void *arg)
{
	ARG_UNUSED(arg);

	concentrator_timer = NULL;
	if (concentrator_period_ms == 0U || !g_zbNwkCtx.joined) {
		return -1;
	}

	(void)route_discovery(NWK_BROADCAST_ROUTER_COORDINATOR, true, false,
			      g_zbNIB.concentratorRadius);
	concentrator_timer = ev_timer_taskPost(concentrator_tick, NULL, concentrator_period_ms);
	return -1;
}

void znp_routing_apply(void)
{
	uint8_t enable = 1U;
	uint8_t period = 120U;
	size_t n;

	(void)znp_nv_read(ZNP_NV_CONCENTRATOR_ENABLE, 0U, &enable, 1U, &n);
	(void)znp_nv_read(ZNP_NV_CONCENTRATOR_DISCOVERY, 0U, &period, 1U, &n);

	g_zbNIB.isConcentrator = (enable != 0U) ? 1U : 0U;
	g_zbNIB.concentratorDiscoveryTime = period;
	concentrator_period_ms = (enable != 0U) ? (uint32_t)period * 1000U : 0U;

	if (concentrator_timer != NULL) {
		(void)ev_timer_taskCancel(&concentrator_timer);
		concentrator_timer = NULL;
	}
	if (concentrator_period_ms != 0U && g_zbNwkCtx.joined) {
		concentrator_timer = ev_timer_taskPost(concentrator_tick, NULL,
						       concentrator_period_ms);
	}
}

/*
 * A channel change is announced to the network, and every node, this one
 * included, moves once the announcement has had time to spread.
 */
#define NWK_UPDATE_MAX_SCAN_DURATION 5U
#define NWK_UPDATE_CHANNEL_CHANGE 0xFEU
#define NWK_UPDATE_SET_MANAGER    0xFFU
#define NWK_UPDATE_BROADCAST_ALL  0xFFFDU
#define NWK_UPDATE_DELAY_MS       5000U

static uint8_t pending_channel;
static uint8_t pending_update_id;

static int channel_change_apply(void *arg)
{
	ARG_UNUSED(arg);

	tl_zbMacChannelSet(pending_channel);
	g_zbMacPib.phyChannelCur = pending_channel;
	g_zbNIB.updateId = pending_update_id;
	zdo_nlmeChannelShift(pending_channel);
	tl_zbNwkBeaconPayloadUpdate();

	/* The host reads the new channel back from the NIB. */
	LOG_INF("znp: network moved to channel %u", pending_channel);
	return -1;
}

static void zdo_mgmt_nwk_update_req(const uint8_t *d, uint8_t len)
{
	zdo_mgmt_nwk_update_req_t req;
	uint32_t channels;
	u8 seq = 0U;

	if (len < 11U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	channels = znp_get_u16(&d[3]) | ((uint32_t)znp_get_u16(&d[5]) << 16);
	if (d[7] <= NWK_UPDATE_MAX_SCAN_DURATION) {
		/* An energy scan of the target: channels, duration, count. */
		uint8_t pl[6];

		memcpy(pl, &d[3], 4U);
		pl[4] = d[7];
		pl[5] = d[8];
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U, zdo_send(znp_get_u16(d), 0x0038U, pl, 6U));
		return;
	}

	if (d[7] == NWK_UPDATE_SET_MANAGER) {
		/* New channel mask and network manager, announced to the network. */
		memset(&req, 0, sizeof(req));
		req.scan_ch = channels;
		req.scan_duration = NWK_UPDATE_SET_MANAGER;
		req.nwk_manager_addr = znp_get_u16(&d[9]);
		aps_ib.aps_channel_mask = channels;
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U,
				(zb_mgmtNwkUpdateReq(NWK_UPDATE_BROADCAST_ALL, &req, &seq, NULL) ==
				 ZDO_SUCCESS)
					? ZNP_STATUS_SUCCESS
					: ZNP_STATUS_FAILURE);
		return;
	}

	if (d[7] != NWK_UPDATE_CHANNEL_CHANGE) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	/* One channel of the 2.4 GHz band, other than the current one. */
	if (channels == 0U || (channels & (channels - 1U)) != 0U ||
	    (channels & ~(0x07FFFFFFUL & ~0x7FFUL)) != 0U) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	pending_channel = (uint8_t)__builtin_ctz(channels);
	pending_update_id = (uint8_t)(g_zbNIB.updateId + 1U);

	memset(&req, 0, sizeof(req));
	req.scan_ch = channels;
	req.scan_duration = NWK_UPDATE_CHANNEL_CHANGE;
	req.nwk_update_id = pending_update_id;
	if (zb_mgmtNwkUpdateReq(NWK_UPDATE_BROADCAST_ALL, &req, &seq, NULL) != ZDO_SUCCESS) {
		znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U, ZNP_STATUS_FAILURE);
		return;
	}

	(void)ev_timer_taskPost(channel_change_apply, NULL, NWK_UPDATE_DELAY_MS);
	znp_srsp_status(ZNP_SUBSYS_ZDO, 0x37U, ZNP_STATUS_SUCCESS);
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
	{ZNP_SUBSYS_ZDO, 0x3FU, zdo_msg_cb_remove},
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

/* The MT message that carries the response of a ZDO cluster. */
static uint8_t rsp_cmd(uint16_t cluster_id)
{
	switch (cluster_id) {
	case 0x8010U: /* complex descriptor */
		return 0x87U;
	case 0x8011U: /* user descriptor */
		return 0x88U;
	case 0x8014U: /* user descriptor set: a confirmation */
		return 0x89U;
	case 0x8015U: /* server discovery */
		return 0x8AU;
	default:
		return (uint8_t)(0x80U + (cluster_id & 0xffU));
	}
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

	if (cb_wanted(cluster_id)) {
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
		znp_areq(ZNP_SUBSYS_ZDO, rsp_cmd(cluster_id), out, 2U + len - 1U);
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
