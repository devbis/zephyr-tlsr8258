/*
 * Port of ../libzigbee/src/zb_api.c.
 *
 * The vendor API is intentionally kept in one translation unit.  Zephyr
 * supplies the OS, buffer and radio hooks; ZDO request serialization is left
 * to the ported zdo_send_req() implementation instead of being reimplemented
 * in each caller.
 */

#include "zb_local.h"
#include "zb_api.h"
#include "af/zb_af.h"
#include "aps/aps_api.h"
#include "zdo/zdo_api.h"
#include "zdo/zdp.h"

static u8 zb_api_zdp_seq;

extern void endDevMacDataPoll(void);

static zdo_status_t zb_zdo_send_short_req(u16 dst_nwk_addr, u16 cluster_id,
						  const void *payload, u8 payload_len,
						  u8 *seq_no, zdo_callback ind_cb)
{
	zdo_zdp_req_t req;
	u8 zdu[ZB_BUF_SIZE];
	u8 seq;

	if (payload == NULL || (u16)payload_len + 1U > sizeof(zdu)) {
		return ZDO_INVALID_REQUEST;
	}

	seq = zb_api_zdp_seq++;
	zdu[0] = seq;
	memcpy(&zdu[1], payload, payload_len);
	memset(&req, 0, sizeof(req));
	req.zdu = zdu;
	req.zduLen = payload_len + 1U;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dst_nwk_addr;
	req.cluster_id = cluster_id;
	req.zdoRspReceivedIndCb = ind_cb;
	req.zdpSeqNum = seq;

	if (seq_no != NULL) {
		*seq_no = seq;
	}

	return (zdo_status_t)zdo_send_req(&req);
}

static zdo_status_t zb_zdo_send_broadcast_req(u16 cluster_id,
						       const void *payload, u8 payload_len,
						       u8 *seq_no, zdo_callback ind_cb)
{
	return zb_zdo_send_short_req(NWK_BROADCAST_RX_ON_WHEN_IDLE, cluster_id,
					     payload, payload_len, seq_no, ind_cb);
}

bool zb_isDeviceFactoryNew(void)
{
	return g_zbNwkCtx.is_factory_new ? TRUE : FALSE;
}

void zb_deviceFactoryNewSet(bool new_value)
{
	g_zbNwkCtx.is_factory_new = new_value ? 1U : 0U;
}

bool zb_isDeviceJoinedNwk(void)
{
	return g_zbNwkCtx.joined ? TRUE : FALSE;
}

bool zb_getMacAssocPermit(void)
{
	return g_zbMacPib.associationPermit ? TRUE : FALSE;
}

void zb_nldeDataDiscoverRouteSet(bool enable)
{
	g_zbNwkCtx.discoverRoute = enable ? 1U : 0U;
}

bool zb_nldeDataDiscoverRouteGet(void)
{
	return g_zbNwkCtx.discoverRoute ? TRUE : FALSE;
}

void zb_apsExtPanidSet(extPANId_t pan_id)
{
	memcpy(aps_ib.aps_use_ext_panid, pan_id, EXT_ADDR_LEN);
}

aps_status_t zb_apsChannelMaskSet(u32 mask)
{
	return apsSetChnMsk(mask);
}

u32 zb_apsChannelMaskGet(void)
{
	return aps_ib.aps_channel_mask;
}

void zdo_nlmeChannelShift(u8 channel)
{
	if ((u8)(channel - 11U) <= 15U) {
		tl_zbMacChannelSet(channel);
		tl_zbTaskPost(zb_info_save, NULL);
	}
}

device_type_t zb_getDeviceType(void)
{
	return af_nodeDevTypeGet();
}

void zb_getLocalExtAddr(addrExt_t ext_addr)
{
	memcpy(ext_addr, g_zbMacPib.extAddress, EXT_ADDR_LEN);
}

u16 zb_getLocalShortAddr(void)
{
	return g_zbMacPib.shortAddress;
}

u16 zb_getParentShortAddr(void)
{
	return tl_zbNeighborParentShortAddrGet();
}

u8 zb_getNwkAddrByExtAddr(addrExt_t ext_addr, u16 *nwk_addr)
{
	return tl_zbShortAddrByExtAddr(nwk_addr, ext_addr, NULL);
}

u8 zb_getExtAddrByNwkAddr(u16 nwk_addr, addrExt_t ext_addr)
{
	return tl_zbExtAddrByShortAddr(nwk_addr, ext_addr, NULL);
}

void zb_nlmeSetLinkStsPeriod(u8 period_in_sec)
{
#if defined(ZB_ROUTER_ROLE)
	if (period_in_sec != 0U) {
		g_zbInfo.nwkNib.linkStatusPeriod = period_in_sec;
	}
#else
	ARG_UNUSED(period_in_sec);
#endif
}

u8 zb_endDeviceSyncReq(void)
{
	endDevMacDataPoll();
	return RET_OK;
}

u8 zb_setPollRate(u32 new_rate)
{
	zdo_set_pollRate(new_rate);
	return RET_OK;
}

u32 zb_getPollRate(void)
{
	return zdo_af_get_syn_rate();
}

u8 zb_nwkFormation(u32 scan_channels, u8 scan_duration)
{
	#if !defined(ZB_ROUTER_ROLE)
	ARG_UNUSED(scan_channels);
	ARG_UNUSED(scan_duration);
	return ZDO_NOT_SUPPORTED;
	#else
	return (u8)zdo_nwkFormationStart(scan_channels, scan_duration);
	#endif
}

#if !defined(ZB_ROUTER_ROLE)
u8 zb_routerStart(void)
{
	return ZDO_NOT_SUPPORTED;
}
#endif

u8 zb_nwkDiscovery(u32 scan_channels, u8 scan_duration,
			  nwkDiscoveryUserCb_t cb)
{
	nlme_nwkDisc_req_t req = {
		.scanChannels = scan_channels,
		.scanDuration = scan_duration,
	};

	return (u8)zdo_nwkDiscoveryStart(&req, cb);
}

void zb_nwkDiscoveryStop(void)
{
	zdo_nwkDiscoveryStop();
}

u8 zb_assocJoinReq(void)
{
	return (u8)zdo_nwkAssocJoinStart();
}

u8 zb_rejoinReq(u32 scan_channels, u8 scan_duration)
{
	return (u8)zdo_nwkRejoinStart(scan_channels, scan_duration);
}

u8 zb_rejoinReqWithBackOff(u32 scan_channels, u8 scan_duration)
{
	return (u8)zdo_nwkRejoinWithBackOff(scan_channels, scan_duration);
}

void zb_rejoinSecModeSet(u8 mode)
{
	if (mode == REJOIN_SECURITY) {
		aps_ib.aps_use_insecure_join = FALSE;
		aps_ib.aps_authenticated = ss_ib.securityLevel ? TRUE : FALSE;
	} else {
		aps_ib.aps_use_insecure_join = TRUE;
		aps_ib.aps_authenticated = FALSE;
	}
}

u8 zb_directJoinReq(u32 scan_channels, u8 scan_duration)
{
	return (u8)zdo_nwkDirectJoinStart(scan_channels, scan_duration);
}

u8 zb_nwkDirectJoinAccept(nlme_directJoin_req_t *req)
{
#if defined(ZB_ROUTER_ROLE)
	return (u8)zdo_nwkDirectJoinAccept(req);
#else
	ARG_UNUSED(req);
	return ZDO_NOT_SUPPORTED;
#endif
}

u8 zb_routeDiscReq(nlme_routeDisc_req_t *req)
{
#if defined(ZB_ROUTER_ROLE)
	return (u8)zdo_routeDiscReq(req);
#else
	ARG_UNUSED(req);
	return ZDO_NOT_SUPPORTED;
#endif
}

u8 zb_nlmeLeaveReq(nlme_leave_req_t *req)
{
	return (u8)zdo_nlmeLeaveReq(req);
}

u8 zb_nlmePermitJoiningRequest(u8 permit_duration)
{
#if defined(ZB_ROUTER_ROLE)
	return (u8)zdo_nlmePermitJoinReq(permit_duration);
#else
	ARG_UNUSED(permit_duration);
	return ZDO_NOT_SUPPORTED;
#endif
}

static void zb_factoryResetNotify(void *arg)
{
	nlme_leave_cnf_t cnf;
	u8 status = (u8)(unsigned long)arg;

	memset(&cnf, 0, sizeof(cnf));
	cnf.status = (status <= 1U) ? NWK_STATUS_SUCCESS : NWK_STATUS_INVALID_REQUEST;
	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpLeaveCnfCb != NULL) {
		zdoAppIndCbLst->zdpLeaveCnfCb(&cnf);
	}
}

void zb_factoryReset(void)
{
	if (!g_zbNwkCtx.is_factory_new) {
		nlme_leave_req_t req;
		zdo_status_t status;

		memset(&req, 0, sizeof(req));
		status = zdo_nlmeLeaveReq(&req);
		if (status == ZDO_SUCCESS) {
			return;
		}

		tl_zbTaskPost(zb_factoryResetNotify, (void *)(unsigned long)status);
		return;
	}

	(void)nv_resetToFactoryNew();
	tl_zbTaskPost(zb_factoryResetNotify, (void *)1);
}

void zb_resetDevice2FN(void)
{
	if (g_zbNwkCtx.joined) {
		zb_factoryReset();
	} else {
		tl_bdbReset2FN();
	}
}

static zdo_status_t zb_zdo_send_req_struct(u16 dst, u16 cluster,
						   const void *req, u8 len,
						   u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_short_req(dst, cluster, req, len, seq, cb);
}

zdo_status_t zb_zdoNwkAddrReq(u16 dst, zdo_nwk_addr_req_t *req,
				      u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, NWK_ADDR_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoIeeeAddrReq(u16 dst, zdo_ieee_addr_req_t *req,
				       u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, IEEE_ADDR_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoSimpleDescReq(u16 dst, zdo_simple_descriptor_req_t *req,
					 u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, SIMPLE_DESC_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoNodeDescReq(u16 dst, zdo_node_descriptor_req_t *req,
				       u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, NODE_DESC_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoPowerDescReq(u16 dst, zdo_power_descriptor_req_t *req,
					 u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, POWER_DESC_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoActiveEpReq(u16 dst, zdo_active_ep_req_t *req,
				       u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, ACTIVE_EP_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoMatchDescReq(u16 dst, zdo_match_descriptor_req_t *req,
					 u8 *seq, zdo_callback cb)
{
	u8 payload[ZB_BUF_SIZE];
	u8 len;
	u8 *out;

	if (req == NULL || req->num_in_clusters > MAX_REQUESTED_CLUSTER_NUMBER ||
		req->num_out_clusters > MAX_REQUESTED_CLUSTER_NUMBER) {
		return ZDO_INVALID_REQUEST;
	}
	out = payload;
	COPY_U16TOBUFFER(out, req->nwk_addr_interest);
	out += 2;
	COPY_U16TOBUFFER(out, req->profile_id);
	out += 2;
	*out++ = req->num_in_clusters;
	memcpy(out, req->cluster_list,
	       (u16)req->num_in_clusters * sizeof(req->cluster_list[0]));
	out += (u16)req->num_in_clusters * sizeof(req->cluster_list[0]);
	*out++ = req->num_out_clusters;
	memcpy(out, &req->cluster_list[req->num_in_clusters],
	       (u16)req->num_out_clusters * sizeof(req->cluster_list[0]));
	out += (u16)req->num_out_clusters * sizeof(req->cluster_list[0]);
	len = (u8)(out - payload);
	return zb_zdo_send_req_struct(dst, MATCH_DESC_REQ_CLID, payload, len, seq, cb);
}

zdo_status_t zb_zdoComplexDescReq(u16 dst, zdo_complex_descriptor_req_t *req,
					  u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, COMPLEX_DESC_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoUserDescReq(u16 dst, zdo_user_descriptor_req_t *req,
				       u8 *seq, zdo_callback cb)
{
	return zb_zdo_send_req_struct(dst, USER_DESC_REQ_CLID, req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoSystemServerDiscoveryReq(u16 server_mask, u8 *seq,
						     zdo_callback cb)
{
	zdo_system_server_discovery_req_t req = {.server_mask = server_mask};

	return zb_zdo_send_broadcast_req(SYSTEM_SERVER_DISCOVERY_REQ_CLID, &req,
						 sizeof(req), seq, cb);
}

zdo_status_t zb_zdoBindUnbindReq(bool binding, zdo_bind_req_t *req,
					 u8 *seq, zdo_callback cb)
{
	if (req == NULL) {
		return ZDO_INVALID_REQUEST;
	}
	return zb_zdo_send_req_struct(req->dst_addr_mode == SHORT_GROUPADDR_NODSTENDPOINT ?
					     NWK_BROADCAST_RX_ON_WHEN_IDLE :
					     g_zbInfo.macPib.coordShortAddress,
					     binding ? BIND_REQ_CLID : UNBIND_REQ_CLID,
					     req, sizeof(*req), seq, cb);
}

zdo_status_t zb_zdoEndDeviceBindReq(zdo_edBindReq_t *req, u8 *seq,
						     zdo_callback cb)
{
	u8 payload[ZB_BUF_SIZE];
	u8 *out;
	u8 len;

	if (req == NULL || req->num_in_clusters > MAX_REQUESTED_CLUSTER_NUMBER ||
		req->num_out_clusters > MAX_REQUESTED_CLUSTER_NUMBER) {
		return ZDO_INVALID_REQUEST;
	}
	out = payload;
	COPY_U16TOBUFFER(out, req->binding_target_addr);
	out += 2;
	COPY_U16TOBUFFER(out, req->profile_id);
	out += 2;
	memcpy(out, req->src_ext_addr, EXT_ADDR_LEN);
	out += EXT_ADDR_LEN;
	*out++ = req->src_endpoint;
	*out++ = req->num_in_clusters;
	memcpy(out, req->in_cluster_lst,
	       (u16)req->num_in_clusters * sizeof(req->in_cluster_lst[0]));
	out += (u16)req->num_in_clusters * sizeof(req->in_cluster_lst[0]);
	*out++ = req->num_out_clusters;
	memcpy(out, req->out_cluster_lst,
	       (u16)req->num_out_clusters * sizeof(req->out_cluster_lst[0]));
	out += (u16)req->num_out_clusters * sizeof(req->out_cluster_lst[0]);
	len = (u8)(out - payload);
	return zb_zdo_send_req_struct(req->binding_target_addr, END_DEVICE_BIND_REQ_CLID,
						     payload, len, seq, cb);
}

zdo_status_t zb_mgmtPermitJoinReq(u16 dst_nwk_addr, u8 permit_duration,
					  u8 tc_significance, u8 *seq, zdo_callback cb)
{
	zdo_mgmt_permit_join_req_t req = {
		.permit_duration = permit_duration,
		.trust_center_significance = tc_significance,
	};

	return zb_zdo_send_req_struct(dst_nwk_addr, MGMT_PERMIT_JOINING_REQ_CLID,
					     &req, sizeof(req), seq, cb);
}

u8 zb_apsmeRequestKeyReq(ss_apsmeRequestKeyReq_t *req)
{
	zb_buf_t *buf;

	if (req == NULL) {
		return RET_INVALID_PARAMETER;
	}

	buf = zb_buf_allocate();
	if (buf == NULL) {
		return RET_NO_MEMORY;
	}

	memcpy(buf, req, sizeof(*req));
	tl_zbTaskPost(ss_apsmeRequestKeyReq, buf);
	return RET_OK;
}

bool zb_bindingTblSearched(u16 cluster_id, u8 src_ep)
{
	return aps_bindingTblMatched(cluster_id, src_ep) ? TRUE : FALSE;
}

void zb_zdoSendParentAnnce(void)
{
#if defined(ZB_ROUTER_ROLE)
	zdo_parent_announce_send();
#endif
}

void zb_joinAFixedNetwork(u8 channel, u16 pan_id, u16 short_addr,
				 u8 *ext_pan_id, u8 *nwk_key, u8 *tc_addr)
{
	u32 scan_channels;

	tl_zbMacChannelSet(channel);
	g_zbMacPib.panId = pan_id;
	g_zbInfo.macPib.panId = pan_id;
	g_zbMacPib.shortAddress = short_addr;
	g_zbInfo.macPib.shortAddress = short_addr;
	g_zbInfo.nwkNib.nwkAddr = short_addr;

	if (ext_pan_id != NULL) {
		memcpy(g_zbInfo.nwkNib.extPANId, ext_pan_id, EXT_ADDR_LEN);
		memcpy(aps_ib.aps_use_ext_panid, ext_pan_id, EXT_ADDR_LEN);
	}
	if (tc_addr != NULL) {
		memcpy(g_zbInfo.macPib.coordExtAddress, tc_addr, EXT_ADDR_LEN);
		memcpy(ss_ib.trust_center_address, tc_addr, EXT_ADDR_LEN);
	}
	if (nwk_key != NULL) {
		zb_preConfigNwkKey((u8 *)nwk_key, FALSE);
	}

	scan_channels = (channel < 32U) ? ((u32)1U << channel) : 0U;
	if (scan_channels != 0U) {
		(void)zdo_nwkRejoinStart(scan_channels,
					 zdo_cfg_attributes.config_nwk_scan_duration);
	} else {
		(void)zdo_nwkAssocJoinStart();
	}
}

void zb_extPanIdRejoin(extPANId_t ext_pan_id)
{
	if (ext_pan_id != NULL) {
		memcpy(aps_ib.aps_use_ext_panid, ext_pan_id, EXT_ADDR_LEN);
	}
}

void zb_preConfigNwkKey(u8 *nwk_key, bool en_trans_key)
{
	ARG_UNUSED(en_trans_key);
	if (nwk_key != NULL) {
		ss_zdoNwkKeyConfigure(nwk_key, 0U, TRUE);
	}
}
