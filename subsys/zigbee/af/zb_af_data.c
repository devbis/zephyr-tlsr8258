/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_af_data.h"
#include "aps.h"
#include "ev_buffer.h"

#define AF_IND_ASDU_LEN_SIZE 2U
#define AF_IND_ASDU_OFFSET   (sizeof(aps_data_ind_t) + AF_IND_ASDU_LEN_SIZE)

uint8_t af_dataSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint16_t clusterId, uint16_t cmdPldLen, uint8_t *cmdPld, uint8_t *apsCnt)
{
	aps_data_req_t req;

	memset(&req, 0, sizeof(req));
	req.src_endpoint = srcEp;
	req.dst_addr_mode = pDstEpInfo->dstAddrMode;
	if (req.dst_addr_mode == APS_SHORT_DSTADDR_WITHEP ||
	    req.dst_addr_mode == APS_LONG_DSTADDR_WITHEP) {
		req.aps_addr.dst_endpoint = pDstEpInfo->dstEp;
		if (req.dst_addr_mode == APS_SHORT_DSTADDR_WITHEP) {
			req.aps_addr.dst_short_addr = pDstEpInfo->dstAddr.shortAddr;
		} else {
			memcpy(&req.aps_addr, &pDstEpInfo->dstAddr, sizeof(pDstEpInfo->dstAddr));
		}
	} else if (req.dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) {
		req.aps_addr.dst_short_addr = pDstEpInfo->dstAddr.shortAddr;
	} else if ((pDstEpInfo->txOptions & APS_TX_OPT_FRAG_PERMITTED) != 0U) {
		return 1;
	}
	req.cluster_id = clusterId;
	req.profile_id = pDstEpInfo->profileId;
	req.tx_options = pDstEpInfo->txOptions;
	req.radius = pDstEpInfo->radius;
	req.enableNWKsecurity = (pDstEpInfo->txOptions & APS_TX_OPT_DISABLE_NWK_KEY) != 0U;
	req.useAlias = pDstEpInfo->useAlias;
	req.aliasSrcAddr = pDstEpInfo->aliasSrcAddr;
	req.aliasSeqNum = pDstEpInfo->aliasSeqNum;
	req.apsCnt = aps_get_counter_value();
	*apsCnt = req.apsCnt;

	if ((req.tx_options & APS_TX_OPT_FRAG_PERMITTED) != 0U) {
		if (aps_ib.aps_fragment_payload_size == 0U) {
			return 1;
		}
		if (aps_ib.aps_fragment_payload_size >= cmdPldLen) {
			req.tx_options &= ~APS_TX_OPT_FRAG_PERMITTED;
		} else {
			if (req.dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) {
				return 11;
			}
			if (req.dst_addr_mode == APS_SHORT_DSTADDR_WITHEP &&
			    (req.aps_addr.dst_short_addr & APS_NWK_BROADCAST_MASK) ==
				    APS_NWK_BROADCAST_MASK) {
				return 11;
			}
			req.tx_options |= APS_TX_OPT_ACK_TX;
			return apsDataFragmentRequest(&req, cmdPld, cmdPldLen);
		}
	}

	return apsDataRequest(&req, cmdPld, (uint8_t)cmdPldLen);
}

void af_dataCnfHandler(void *arg)
{
	apsdeDataConf_t *cnf = (apsdeDataConf_t *)arg;
	af_endpoint_descriptor_t *zdoEp = af_zdoSimpleDescriptorGet();
	af_endpoint_descriptor_t *epList = af_epDescriptorGet();
	uint8_t epNum = af_availableEpNumGet();

	if (zdoEp->cb_cnf != NULL && cnf->srcEndpoint == ZDO_EP) {
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

	/* The receive callbacks take an apsdeDataInd_t: the indication, then the 16-bit
	 * ASDU length, then the ASDU itself.
	 * Deliberate deviation from the vendor object: it computes the size as a byte from
	 * the low byte of asduLength (af_aps_data_entry offsets 0x5e-0x68), so an ASDU of
	 * 219 bytes or more wraps the size and the copy overflows the buffer.
	 */
	stagingSize = (uint16_t)(sizeof(*ind) + AF_IND_ASDU_LEN_SIZE + ind->asduLength);
	stagingInd = (aps_data_ind_t *)ev_buf_allocate(stagingSize);
	if (stagingInd == NULL) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	memcpy(stagingInd, ind, sizeof(*stagingInd));
	COPY_U16TOBUFFER((uint8_t *)stagingInd + sizeof(*stagingInd), ind->asduLength);
	memcpy((uint8_t *)stagingInd + AF_IND_ASDU_OFFSET, ind->asdu, ind->asduLength);
	/* Deliberate deviation from the vendor object: it leaves asdu pointing at the
	 * receive buffer that is freed after the callback; point it at the copy.
	 */
	stagingInd->asdu = (uint8_t *)stagingInd + AF_IND_ASDU_OFFSET;

	for (uint8_t i = 0; i < epNum; i++) {
		af_endpoint_descriptor_t *ep = &epList[i];
		af_simple_descriptor_t *simple = ep->correspond_simple_desc;

		if (ep->ep == ind->dst_ep) {
			if (!af_profileIdMatched(ind->profile_id, simple) &&
			    ind->profile_id != LL_PROFILE_ID) {
				continue;
			}
			if (ep->cb_rx == NULL) {
				break;
			}
			stagingInd->profile_id = (ind->profile_id == WILDCARD_PROFILE_ID)
							 ? simple->app_profile_id
							 : ind->profile_id;
			ep->cb_rx(stagingInd);
			zb_buf_free((zb_buf_t *)arg);
			return;
		}

		if (ind->dst_ep != AF_BROADCAST_EP) {
			continue;
		}

		if (!af_profileIdMatched(ind->profile_id, simple) &&
		    ind->profile_id != LL_PROFILE_ID) {
			continue;
		}

		if (ep->cb_rx == NULL) {
			continue;
		}

		stagingInd->profile_id = (ind->profile_id == WILDCARD_PROFILE_ID)
						 ? simple->app_profile_id
						 : ind->profile_id;
		stagingInd->dst_ep = ep->ep;

		aps_data_ind_t *callbackInd = (aps_data_ind_t *)ev_buf_allocate(stagingSize);
		if (callbackInd == NULL) {
			break;
		}

		memcpy(callbackInd, stagingInd, stagingSize);
		/* Deliberate deviation from the vendor object: the vendor leaves asdu pointing at
		 * the receive buffer, which is freed after the loop. Each copy owns its ASDU, so
		 * point it at the copy.
		 */
		callbackInd->asdu = (uint8_t *)callbackInd + AF_IND_ASDU_OFFSET;
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

		if (ind->profile_id == WILDCARD_PROFILE_ID) {
			ind->profile_id = simple->app_profile_id;
		}

		ep->cb_rx(arg);
		return;
	}

	ev_buf_free((uint8_t *)arg);
}
