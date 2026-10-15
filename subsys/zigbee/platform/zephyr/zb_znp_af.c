/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * ZNP AF commands: endpoint registration, data requests with their
 * confirmations, and incoming application frames.
 */
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "zb_common.h"
#include "aps/aps_data.h"
#include "ss/ss_security_flags.h"
#include "zb_znp_internal.h"

LOG_MODULE_DECLARE(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZNP_AF_REGISTER         0x00U
#define ZNP_AF_DATA_REQUEST     0x01U
#define ZNP_AF_DATA_REQUEST_EXT 0x02U
#define ZNP_AF_DATA_CONFIRM     0x80U
#define ZNP_AF_INCOMING_MSG     0x81U

#define ZNP_ADDR_MODE_GROUP     0x01U
#define ZNP_ADDR_MODE_NWK       0x02U
#define ZNP_ADDR_MODE_IEEE      0x03U
#define ZNP_ADDR_MODE_BROADCAST 0x0FU

#define ZNP_AF_OPT_ACK_REQUEST  0x10U

#define ZNP_STATUS_APS_NO_ACK   0xB7U
#define ZNP_STATUS_APS_FAIL     0xB1U

#define ZNP_MAX_ENDPOINTS       16U
#define ZNP_MAX_CLUSTERS        16U
#define ZNP_MAX_PENDING         8U

struct znp_endpoint {
	bool used;
	af_simple_descriptor_t desc;
	uint16_t in[ZNP_MAX_CLUSTERS];
	uint16_t out[ZNP_MAX_CLUSTERS];
};

/* A request waiting for its confirmation from the stack. */
struct znp_pending {
	bool used;
	uint8_t aps_cnt;
	uint8_t src_ep;
	uint8_t tsn;
};

static struct znp_endpoint endpoints[ZNP_MAX_ENDPOINTS];
static struct znp_pending pending[ZNP_MAX_PENDING];

static uint8_t map_aps_status(uint8_t aps)
{
	switch (aps) {
	case APS_STATUS_SUCCESS:
		return ZNP_STATUS_SUCCESS;
	case APS_STATUS_NO_ACK:
		return ZNP_STATUS_APS_NO_ACK;
	default:
		return ZNP_STATUS_APS_FAIL;
	}
}

static void data_confirm(uint8_t status, uint8_t ep, uint8_t tsn)
{
	uint8_t d[3] = {status, ep, tsn};

	znp_areq(ZNP_SUBSYS_AF, ZNP_AF_DATA_CONFIRM, d, sizeof(d));
}

/* Confirmation from the stack, for every endpoint the host registered. */
static void znp_ep_cnf(void *arg)
{
	const apsdeDataConf_t *cnf = (const apsdeDataConf_t *)arg;

	for (size_t i = 0; i < ARRAY_SIZE(pending); i++) {
		if (pending[i].used && pending[i].aps_cnt == cnf->apsCnt &&
		    pending[i].src_ep == cnf->srcEndpoint) {
			pending[i].used = false;
			data_confirm(map_aps_status(cnf->status), cnf->srcEndpoint, pending[i].tsn);
			break;
		}
	}

	ev_buf_free((u8 *)arg);
}

/* An application frame for a host endpoint. */
static void znp_ep_rx(void *arg)
{
	const apsdeDataInd_t *ind = (const apsdeDataInd_t *)arg;
	const aps_data_ind_t *info = &ind->indInfo;
	uint8_t out[ZNP_MAX_DATA];
	uint8_t *p = out;
	bool group = info->dst_addr_mode == APS_SHORT_GROUPADDR_NOEP;
	uint8_t n = (uint8_t)MIN(ind->asduLen, ZNP_MAX_DATA - 26U);

	p = znp_put_u16(p, group ? info->dst_addr : 0U);
	p = znp_put_u16(p, info->cluster_id);
	p = znp_put_u16(p, info->src_short_addr);
	*p++ = info->src_ep;
	*p++ = info->dst_ep;
	*p++ = ZB_NWK_IS_ADDRESS_BROADCAST(info->dst_addr) ? 1U : 0U;
	*p++ = info->lqi;
	*p++ = (info->security_status != 0U) ? 1U : 0U;
	p = znp_put_u32(p, info->rx_tick);
	*p++ = info->aps_counter;
	*p++ = n;
	memcpy(p, ind->asdu, n);
	p += n;
	p = znp_put_u16(p, info->srcMacAddr);
	*p++ = 0U; /* radius */

	znp_areq(ZNP_SUBSYS_AF, ZNP_AF_INCOMING_MSG, out, (size_t)(p - out));
	ev_buf_free((u8 *)arg);
}

static void af_register(const uint8_t *d, uint8_t len)
{
	struct znp_endpoint *e = NULL;
	uint8_t in_count;
	uint8_t out_count;
	size_t off;

	if (len < 8U) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_REGISTER, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	in_count = d[7];
	off = 8U + (size_t)in_count * 2U;
	if (in_count > ZNP_MAX_CLUSTERS || len < off + 1U) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_REGISTER, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}
	out_count = d[off];
	if (out_count > ZNP_MAX_CLUSTERS || len < off + 1U + (size_t)out_count * 2U) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_REGISTER, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	/* Re-registering an endpoint replaces it, whatever registered it first. */
	for (size_t i = 0; i < ARRAY_SIZE(endpoints); i++) {
		if (endpoints[i].used && endpoints[i].desc.endpoint == d[0]) {
			e = &endpoints[i];
			(void)af_endpointUnregister(d[0]);
			break;
		}
	}
	if (e == NULL) {
		(void)af_endpointUnregister(d[0]);
		for (size_t i = 0; i < ARRAY_SIZE(endpoints); i++) {
			if (!endpoints[i].used) {
				e = &endpoints[i];
				break;
			}
		}
	}
	if (e == NULL) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_REGISTER, ZNP_STATUS_MEM_ERROR);
		return;
	}

	memset(e, 0, sizeof(*e));
	e->desc.endpoint = d[0];
	e->desc.app_profile_id = znp_get_u16(&d[1]);
	e->desc.app_dev_id = znp_get_u16(&d[3]);
	e->desc.app_dev_ver = d[5] & 0x0fU;
	e->desc.app_in_cluster_count = in_count;
	e->desc.app_out_cluster_count = out_count;
	for (uint8_t i = 0; i < in_count; i++) {
		e->in[i] = znp_get_u16(&d[8U + (size_t)i * 2U]);
	}
	for (uint8_t i = 0; i < out_count; i++) {
		e->out[i] = znp_get_u16(&d[off + 1U + (size_t)i * 2U]);
	}
	e->desc.app_in_cluster_lst = e->in;
	e->desc.app_out_cluster_lst = e->out;

	if (!af_endpointRegister(d[0], &e->desc, znp_ep_rx, znp_ep_cnf)) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_REGISTER, ZNP_STATUS_MEM_ERROR);
		return;
	}

	e->used = true;
	znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_REGISTER, ZNP_STATUS_SUCCESS);
}

static uint16_t src_profile(uint8_t ep)
{
	const af_simple_descriptor_t *desc = af_simpleDescGet(ep);

	return (desc != NULL) ? desc->app_profile_id : HA_PROFILE_ID;
}

/*
 * Send an application frame through the APS layer, which tracks its
 * acknowledgement and retries, and confirms it to the source endpoint.
 */
static u8 aps_send(uint8_t addr_mode, const uint8_t *addr, uint8_t dst_ep, uint8_t src_ep,
		   uint16_t cluster, uint8_t options, uint8_t radius, const uint8_t *data,
		   uint16_t data_len, u8 *aps_cnt)
{
	aps_data_req_t req;

	memset(&req, 0, sizeof(req));
	req.src_endpoint = src_ep;
	req.cluster_id = cluster;
	req.profile_id = src_profile(src_ep);
	req.radius = radius;
	req.apsCnt = aps_get_counter_value();
	req.enableNWKsecurity = (ss_ib_security_level_get() != 0U);
	req.tx_options = ((options & ZNP_AF_OPT_ACK_REQUEST) != 0U) ? APS_TX_OPT_ACK_TX : 0U;
	*aps_cnt = req.apsCnt;

	switch (addr_mode) {
	case ZNP_ADDR_MODE_GROUP:
		req.dst_addr_mode = APS_SHORT_GROUPADDR_NOEP;
		req.aps_addr.dst_group_addr = znp_get_u16(addr);
		break;
	case ZNP_ADDR_MODE_IEEE:
		req.dst_addr_mode = APS_LONG_DSTADDR_WITHEP;
		memcpy(req.aps_addr.dst_ext_addr, addr, 8U);
		req.aps_addr.dst_endpoint = dst_ep;
		break;
	default:
		req.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
		req.aps_addr.dst_short_addr = znp_get_u16(addr);
		req.aps_addr.dst_endpoint = dst_ep;
		break;
	}

	if (data_len > aps_ib.aps_fragment_payload_size &&
	    req.dst_addr_mode != APS_SHORT_GROUPADDR_NOEP) {
		req.tx_options |= APS_TX_OPT_ACK_TX;
		return apsDataFragmentRequest(&req, (u8 *)data, data_len);
	}

	return apsDataRequest(&req, (u8 *)data, (u8)data_len);
}

/* A ZDO frame has no APS acknowledgement to track, so it is sent directly. */
static u8 zdo_frame_send(uint16_t dst, uint16_t cluster, uint8_t radius, const uint8_t *data,
			 uint16_t data_len, u8 *aps_cnt)
{
	epInfo_t ep;

	memset(&ep, 0, sizeof(ep));
	ep.dstAddr.shortAddr = dst;
	ep.dstEp = ZDO_EP;
	ep.profileId = ZDO_PROFILE_ID;
	ep.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	ep.radius = radius;

	return af_dataSend(ZDO_EP, &ep, cluster, data_len, (u8 *)data, aps_cnt);
}

static void data_request(uint8_t cmd, uint8_t addr_mode, const uint8_t *addr, uint8_t dst_ep,
			 uint8_t src_ep, uint16_t cluster, uint8_t tsn, uint8_t options,
			 uint8_t radius, const uint8_t *data, uint16_t data_len)
{
	u8 aps_cnt = 0U;
	u8 status;

	if (dst_ep == ZDO_EP && addr_mode != ZNP_ADDR_MODE_GROUP) {
		status = zdo_frame_send(znp_get_u16(addr), cluster, radius, data, data_len,
					&aps_cnt);
	} else {
		status = aps_send(addr_mode, addr, dst_ep, src_ep, cluster, options, radius, data,
				  data_len, &aps_cnt);
	}
	if (status != APS_STATUS_SUCCESS) {
		znp_srsp_status(ZNP_SUBSYS_AF, cmd, map_aps_status(status));
		return;
	}

	znp_srsp_status(ZNP_SUBSYS_AF, cmd, ZNP_STATUS_SUCCESS);

	if (dst_ep == ZDO_EP && addr_mode != ZNP_ADDR_MODE_GROUP) {
		/* A ZDO request has no confirmation. */
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(pending); i++) {
		if (!pending[i].used) {
			pending[i].used = true;
			pending[i].aps_cnt = aps_cnt;
			pending[i].src_ep = src_ep;
			pending[i].tsn = tsn;
			return;
		}
	}

	/* No room to track it, so confirm right away. */
	data_confirm(ZNP_STATUS_SUCCESS, src_ep, tsn);
}

static void af_data_request(const uint8_t *d, uint8_t len)
{
	/* dst(2) dst ep, src ep, cluster(2), tsn, options, radius, length, data */
	if (len < 10U || len < 10U + d[9]) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_DATA_REQUEST, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	data_request(ZNP_AF_DATA_REQUEST, ZNP_ADDR_MODE_NWK, d, d[2], d[3], znp_get_u16(&d[4]),
		     d[6], d[7], d[8], &d[10], d[9]);
}

static void af_data_request_ext(const uint8_t *d, uint8_t len)
{
	uint16_t data_len;

	/* mode, address(8), dst ep, pan(2), src ep, cluster(2), tsn, options, radius, length(2) */
	if (len < 20U) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_DATA_REQUEST_EXT, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	data_len = znp_get_u16(&d[18]);
	if (len < 20U + data_len) {
		znp_srsp_status(ZNP_SUBSYS_AF, ZNP_AF_DATA_REQUEST_EXT, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	data_request(ZNP_AF_DATA_REQUEST_EXT, d[0], &d[1], d[9], d[12], znp_get_u16(&d[13]), d[15],
		     d[16], d[17], &d[20], data_len);
}

static const struct znp_cmd af_cmds[] = {
	{ZNP_SUBSYS_AF, ZNP_AF_REGISTER, af_register},
	{ZNP_SUBSYS_AF, ZNP_AF_DATA_REQUEST, af_data_request},
	{ZNP_SUBSYS_AF, ZNP_AF_DATA_REQUEST_EXT, af_data_request_ext},
};

bool znp_af_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len)
{
	return znp_cmd_dispatch(af_cmds, ARRAY_SIZE(af_cmds), subsys, cmd, data, len);
}
