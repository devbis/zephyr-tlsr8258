/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_af_data.h"
#include "aps.h"
#include "ev_buffer.h"

uint8_t af_dataSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint16_t clusterId, uint16_t cmdPldLen, uint8_t *cmdPld, uint8_t *apsCnt)
{
	aps_data_req_t req;

	memset(&req, 0, sizeof(req));
	req.src_endpoint = srcEp;
	req.cluster_id = clusterId;
	req.asdu_length = (uint8_t)cmdPldLen;
	req.dst_addr_mode = pDstEpInfo->dstAddrMode;
	req.tx_options = pDstEpInfo->txOptions;
	req.radius = pDstEpInfo->radius;
	req.profile_id = pDstEpInfo->profileId;
	req.apsCnt = aps_get_counter_value();
	req.useAlias = pDstEpInfo->useAlias;
	req.aliasSrcAddr = pDstEpInfo->aliasSrcAddr;
	req.aliasSeqNum = pDstEpInfo->aliasSeqNum;
	if (req.dst_addr_mode == APS_SHORT_DSTADDR_WITHEP) {
		req.aps_addr.dst_short_addr = pDstEpInfo->dstAddr.shortAddr;
	} else {
		memcpy(&req.aps_addr, &pDstEpInfo->dstAddr, sizeof(pDstEpInfo->dstAddr));
	}
	req.aps_addr.dst_endpoint = pDstEpInfo->dstEp;
	*apsCnt = req.apsCnt;

	if ((uint16_t)cmdPldLen > aps_ib.aps_fragment_payload_size &&
	    pDstEpInfo->dstAddrMode != APS_SHORT_GROUPADDR_NOEP) {
		req.tx_options |= APS_TX_OPT_ACK_TX;
		return apsDataFragmentRequest(&req, cmdPld, cmdPldLen);
	}

	return apsDataRequest(&req, cmdPld, (uint8_t)cmdPldLen);
}

void af_dataCnfHandler(void *arg)
{
	apsdeDataConf_t *cnf = (apsdeDataConf_t *)arg;
	af_endpoint_descriptor_t *zdoEp = af_zdoSimpleDescriptorGet();
	af_endpoint_descriptor_t *epList = af_epDescriptorGet();
	uint8_t epNum = af_availableEpNumGet();

	if (zdoEp->cb_cnf != NULL && cnf->srcEndpoint == zdoEp->ep) {
		zdoEp->cb_cnf(arg);
	} else {
		for (uint8_t i = 0; i < epNum; i++) {
			if (epList[i].cb_cnf != NULL && epList[i].ep == cnf->srcEndpoint) {
				epList[i].cb_cnf(arg);
				break;
			}
		}
	}

	ev_buf_free((uint8_t *)arg);
}

void af_aps_data_entry(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	af_endpoint_descriptor_t *epList;
	uint8_t epNum;
	aps_data_ind_t *stagingInd;
	uint16_t stagingSize;

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

	stagingSize = (uint16_t)(sizeof(*ind) + ind->asduLength);
	stagingInd = (aps_data_ind_t *)ev_buf_allocate(stagingSize);
	if (stagingInd == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memcpy(stagingInd, ind, sizeof(*stagingInd));
	stagingInd->asdu = (uint8_t *)stagingInd + sizeof(*stagingInd);
	memcpy(stagingInd->asdu, ind->asdu, ind->asduLength);

	for (uint8_t i = 0; i < epNum; i++) {
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
			ev_buf_free((uint8_t *)stagingInd);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		memcpy(callbackInd, stagingInd, stagingSize);
		callbackInd->asdu = (uint8_t *)callbackInd + sizeof(*callbackInd);
		ep->cb_rx(callbackInd);
	}

	ev_buf_free((uint8_t *)stagingInd);
	zb_buf_free((zb_buf_t *)arg);
}

void af_aps_data_fragment_entry(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	af_endpoint_descriptor_t *epList = af_epDescriptorGet();
	uint8_t epNum = af_availableEpNumGet();

	for (uint8_t i = 0; i < epNum; i++) {
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

	ev_buf_free((uint8_t *)arg);
}
