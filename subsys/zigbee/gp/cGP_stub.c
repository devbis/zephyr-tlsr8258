/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "gp_internal.h"

#if defined(ZB_ROUTER_ROLE)
u8 cgp_mcpsDataIndFilter(u8 *p)
{
	u8 first = p[0];
	u8 frameType = first & 0x03U;
	u8 ret = 1;
	u8 second;
	u8 appId;

	if ((first & 0x3cU) != 0x0cU) {
		return ret;
	}

	if (frameType > GP_NWK_FRAME_TYPE_MAINTENANCE) {
		return ret;
	}

	ret = 0;
	if ((first & 0x80U) == 0U) {
		return ret;
	}

	ret = 1;
	if (frameType == GP_NWK_FRAME_TYPE_MAINTENANCE) {
		return ret;
	}

	second = p[1];
	if ((first & 0x40U) != 0U && (second & 0x40U) != 0U) {
		return ret;
	}

	appId = second & 0x07U;
	if (appId == GP_APP_ID_GPD) {
		return second >> 7;
	}

	if (appId == GP_APP_ID_SRC_ID) {
		return second >> 7;
	}

	return ret;
}

void cGp_mcpsDataInd(void *arg)
{
	u8 *buf = (u8 *)arg;
	zb_mscp_data_ind_t saved;
	dgp_data_ind_t *out = (dgp_data_ind_t *)arg;
	u8 rssi;

	rssi = ((zb_buf_t *)arg)->hdr.rssi;
	memcpy(&saved, buf, sizeof(saved));

	if (cgp_mcpsDataIndFilter((u8 *)saved.msdu) != 0U) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	out->gpMpdu = saved.msdu;
	out->srcPanId = saved.srcPanId;
	out->dstPanId = saved.dstPanId;
	memcpy(&out->srcAddr, &saved.srcAddr.addr, sizeof(out->srcAddr));
	memcpy(&out->dstAddr, &saved.dstAddr.addr, sizeof(out->dstAddr));
	out->srcAddrMode = saved.srcAddr.addrMode;
	out->dstAddrMode = saved.dstAddr.addrMode;
	out->rssi = (s8)rssi;
	out->lqi = saved.mpduLinkQuality;
	out->seqNum = saved.dsn;
	out->gpMpduLen = saved.msduLength;

	tl_zbTaskPost(dGp_dataInd, arg);
}

void cGp_dataCnf(void *arg)
{
	cGpDataCnfHandler(arg);
}

int cGp_dataReq(void *arg)
{
	u8 *buf = (u8 *)arg;
	cgp_data_req_t saved;
	zb_mscp_data_req_t *out = (zb_mscp_data_req_t *)arg;
	u8 txOptions;

	memcpy(&saved, buf, sizeof(saved));
	txOptions = *((u8 *)&saved.txOptions);

	memset(out, 0, sizeof(*out));
	out->dstPanId = saved.dstPanId;
	out->srcAddr.addrMode = saved.srcAddrMode;
	memcpy(&out->dstAddr.addr, &saved.dstAddr, sizeof(saved.dstAddr));
	out->dstAddr.addrMode = saved.dstAddrMode;
	out->msduHandle = saved.gpMpduHandle;
	out->msduLength = saved.gpMpduLen;
	out->msdu = saved.gpMpdu;

	if ((txOptions & 0x02U) != 0U) {
		out->txOptions |= 0x01U;
	}
	if ((txOptions & 0x01U) != 0U) {
		out->txOptions |= 0x08U;
	}

	tl_zbMacMcpsDataRequestProc(arg);
	return -1;
}
#else
/* Empty translation unit: the original end-device object exported no symbols. */
#endif
