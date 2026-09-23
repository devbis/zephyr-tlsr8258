/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_af_data.h"
#include "aps.h"
#include "ev_buffer.h"

u8 af_dataSend(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 cmdPldLen, u8 *cmdPld, u8 *apsCnt)
{
	aps_data_req_t req;

	memset(&req, 0, sizeof(req));
	req.src_endpoint = srcEp;
	req.cluster_id = clusterId;
	req.asdu_length = (u8)cmdPldLen;
	req.dst_addr_mode = pDstEpInfo->dstAddrMode;
	req.tx_options = pDstEpInfo->txOptions;
	req.radius = pDstEpInfo->radius;
	req.profile_id = pDstEpInfo->profileId;
	req.apsCnt = aps_get_counter_value();
	req.handle = req.apsCnt;
	req.useAlias = pDstEpInfo->useAlias;
	req.aliasSrcAddr = pDstEpInfo->aliasSrcAddr;
	req.aliasSeqNum = pDstEpInfo->aliasSeqNum;
	req.aps_addr.dst_short_addr = pDstEpInfo->dstAddr.shortAddr;
	req.aps_addr.dst_endpoint = pDstEpInfo->dstEp;

	if (apsCnt != NULL) {
		*apsCnt = req.apsCnt;
	}

	if ((u16)cmdPldLen > aps_ib.aps_fragment_payload_size &&
	    pDstEpInfo->dstAddrMode != APS_SHORT_GROUPADDR_NOEP) {
		req.tx_options |= APS_TX_OPT_ACK_TX;
		return apsDataFragmentRequest(&req, cmdPld, cmdPldLen);
	}

	return apsDataRequest(&req, cmdPld, (u8)cmdPldLen);
}

void af_dataCnfHandler(void *arg)
{
	apsdeDataConf_t *cnf = (apsdeDataConf_t *)arg;
	af_endpoint_descriptor_t *zdoEp = af_zdoSimpleDescriptorGet();
	af_endpoint_descriptor_t *epList = af_epDescriptorGet();
	u8 epNum = af_availableEpNumGet();

	if (zdoEp->cb_cnf != NULL && cnf->srcEndpoint == zdoEp->ep) {
		zdoEp->cb_cnf(arg);
		return;
	}

	for (u8 i = 0; i < epNum; i++) {
		if (epList[i].cb_cnf != NULL && epList[i].ep == cnf->srcEndpoint) {
			epList[i].cb_cnf(arg);
			return;
		}
	}

	ev_buf_free((u8 *)arg);
}

void af_aps_data_entry(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	af_endpoint_descriptor_t *epList;
	u8 epNum;
	aps_data_ind_t *stagingInd;
	u16 stagingSize;

	if (ind->dst_ep == ZDO_EP) {
		af_endpoint_descriptor_t *zdoEp;

		if (ind->profile_id != ZDO_PROFILE_ID) {
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		zdoEp = af_zdoSimpleDescriptorGet();
		zdoEp->cb_rx(arg);
		return;
	}

	epList = af_epDescriptorGet();
	epNum = af_availableEpNumGet();
	if (epNum == 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	stagingSize = (u16)(sizeof(*ind) + ind->asduLength);
	stagingInd = (aps_data_ind_t *)ev_buf_allocate(stagingSize);
	if (stagingInd == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memcpy(stagingInd, ind, sizeof(*stagingInd));
	stagingInd->asdu = (u8 *)stagingInd + sizeof(*stagingInd);
	memcpy(stagingInd->asdu, ind->asdu, ind->asduLength);

	for (u8 i = 0; i < epNum; i++) {
		af_endpoint_descriptor_t *ep = &epList[i];
		af_simple_descriptor_t *simple = ep->correspond_simple_desc;

		if (ep->ep != ind->dst_ep || ep->cb_rx == NULL) {
			continue;
		}

		if (!af_profileIdMatched(ind->profile_id, simple) &&
		    ind->profile_id != LL_PROFILE_ID) {
			continue;
		}

		stagingInd->dst_ep = ep->ep;
		if (ind->profile_id == LL_PROFILE_ID) {
			stagingInd->profile_id = simple->app_profile_id;
		}

		aps_data_ind_t *callbackInd = (aps_data_ind_t *)ev_buf_allocate(stagingSize);
		if (callbackInd == NULL) {
			ev_buf_free((u8 *)stagingInd);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		memcpy(callbackInd, stagingInd, stagingSize);
		callbackInd->asdu = (u8 *)callbackInd + sizeof(*callbackInd);
		ep->cb_rx(callbackInd);
	}

	ev_buf_free((u8 *)stagingInd);
	zb_buf_free((zb_buf_t *)arg);
}

void af_aps_data_fragment_entry(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	af_endpoint_descriptor_t *epList = af_epDescriptorGet();
	u8 epNum = af_availableEpNumGet();

	for (u8 i = 0; i < epNum; i++) {
		af_endpoint_descriptor_t *ep = &epList[i];
		af_simple_descriptor_t *simple = ep->correspond_simple_desc;

		if (ep->ep != ind->dst_ep) {
			continue;
		}

		if (!af_profileIdMatched(ind->profile_id, simple) &&
		    ind->profile_id != LL_PROFILE_ID) {
			continue;
		}

		if (ep->cb_rx == NULL) {
			break;
		}

		if (ind->profile_id == LL_PROFILE_ID && simple != NULL) {
			ind->profile_id = simple->app_profile_id;
		}

		ep->cb_rx(arg);
		return;
	}

	ev_buf_free((u8 *)arg);
}
