/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_nwk_core.h"
#include "zb_nwk_addr_map.h"
#include "nwk_data.h"
#include "nwk_join.h"
#include "zb_nwk_neighbor.h"
#include "nwk_routing.h"
#include "zb_nwk_addr_conflict.h"
#include "zdo_nwk_manager.h"
#include "zdp_services.h"
#include "ss_security_flags.h"

zb_buf_t *conflictInfo = NULL;
ev_timer_event_t *addrConflictHandleTimer = NULL;

#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
static inline bool nwk_neighbor_is_child_end_device(const zb_normal_neighbor_entry_t *entry)
{
	return (entry != NULL) && (entry->deviceType == NWK_DEVICE_TYPE_ED) &&
	       (entry->relationship == NEIGHBOR_IS_CHILD);
}

static void nwk_addrConflictHdrInit(nwk_hdr_t *hdr, uint16_t dstAddr, bool forceSeqNum)
{
	memset(hdr, 0, sizeof(*hdr));

	hdr->dstAddr = dstAddr;
	hdr->srcAddr = g_zbNIB.nwkAddr;
	hdr->radius = (uint8_t)(g_zbNIB.maxDepth << 1);
	hdr->seqNum = forceSeqNum ? 0U : g_zbNIB.seqNum++;
	hdr->frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr->frameControl.protocolVer = ZB_PROTOCOL_VERSION;
	hdr->frameControl.srcIEEEAddr = 1;
	ZB_IEEE_ADDR_COPY(hdr->srcIeeeAddr, g_zbInfo.macPib.extAddress);
}

static inline void nwk_addrConflictStatusSend(zb_buf_t *buf, uint16_t dstAddr, uint16_t statusAddr,
					      bool forceSeqNum)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;

	if (buf == NULL) {
		return;
	}

	nwk_addrConflictHdrInit(&hdr, dstAddr, forceSeqNum);
	if (ss_ib_secure_all_fresh() && (ss_ib_security_level_get() != 0U) &&
	    ss_keyPreconfigured()) {
		hdr.frameControl.security = 1;
	}
	hdr.frameHdrLen = getNwkHdrSize(&hdr);
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmdId = NWK_CMD_NETWORK_STATUS;
	cmd.nwkStatus.dstAddr = statusAddr;
	cmd.nwkStatus.statusCode = NWK_COMMAND_STATUS_ADDRESS_CONFLICT;
	zb_NwkSendNwkStatusCmd(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}

int nwk_addrConflictCb(void *arg)
{
	(void)arg;

	if (conflictInfo != NULL) {
		uint16_t conflictAddr;

		COPY_BUFFERTOU16(conflictAddr, conflictInfo->buf);

		zb_buf_clear(conflictInfo);
		nwk_addrConflictStatusSend(conflictInfo, NWK_BROADCAST_RX_ON_WHEN_IDLE,
					   conflictAddr, FALSE);
		conflictInfo = NULL;
	}

	addrConflictHandleTimer = NULL;

	return -1;
}

void zb_NwkNeighborAddrConflictHandle(zb_buf_t *buf, zb_normal_neighbor_entry_t *neighbor)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	addrExt_t extAddr;
	uint16_t newShortAddr;
	uint16_t oldShortAddr;

	newShortAddr = zb_NwkStochasticAddrCal();
	oldShortAddr = zb_shortAddrByIdx(neighbor->addrmapIdx);
	zb_ExtAddrByIdx(neighbor->addrmapIdx, extAddr);

	nwk_addrConflictHdrInit(&hdr, oldShortAddr, FALSE);
	if (ss_ib_secure_all_fresh() &&
	    ((ss_ib_security_level_get() & SS_IB_SECURITY_LEVEL_ENCRYPTION_MASK) != 0U) &&
	    ss_keyPreconfigured()) {
		hdr.frameControl.security = 1;
	}
	if (!ZB_IEEE_ADDR_IS_ZERO(extAddr)) {
		hdr.frameControl.dstIEEEAddr = 1;
		ZB_IEEE_ADDR_COPY(hdr.dstIeeeAddr, extAddr);
	}
	hdr.frameHdrLen = getNwkHdrSize(&hdr);

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmdId = NWK_CMD_REJOIN_RESPONSE;
	cmd.rejoinRsp.nwkAddr = newShortAddr;
	cmd.rejoinRsp.rejoinStatus = MAC_SUCCESS;
	zb_NwkSendRejoinRespCmd(buf, &hdr, &cmd, neighbor->rxOnWhileIdle,
				  NWK_INTERNAL_REJOIN_RESP_CMD_HANDLE);

	g_nwkAddrMap.addrMap[neighbor->addrmapIdx].shortAddr = newShortAddr;
}

void zb_NwkAddrConflictStatusSend(uint16_t dstAddr, uint16_t statusAddr, uint8_t forceSeqNum)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf != NULL) {
		nwk_addrConflictStatusSend(buf, dstAddr, statusAddr, forceSeqNum != 0U);
	}
}

void zb_NwkAddrConflictHandle(zb_buf_t *buf, uint16_t nwkAddr, zb_normal_neighbor_entry_t *neighbor)
{
	if (addrConflictHandleTimer != NULL) {
		ev_timer_taskCancel(&addrConflictHandleTimer);
	}

	if (conflictInfo != NULL) {
		zb_buf_free(conflictInfo);
	}

	conflictInfo = buf;
	COPY_U16TOBUFFER(buf, nwkAddr);
	addrConflictHandleTimer =
		ev_timer_taskPost(nwk_addrConflictCb, buf, (drv_u32Rand() & 0x3fU) + 1U);

	if (neighbor != NULL) {
		if (nwk_neighbor_is_child_end_device(neighbor)) {
			zb_buf_t *rejoinBuf = zb_buf_allocate();

			if (rejoinBuf != NULL) {
				zb_NwkNeighborAddrConflictHandle(rejoinBuf, neighbor);
			}
		}

		return;
	}

	{
		uint16_t addrRef = 0;
		uint16_t newShortAddr = zb_NwkStochasticAddrCal();

		g_zbNIB.nwkAddr = newShortAddr;
		g_zbMacPib.shortAddress = newShortAddr;
		(void)zb_NwkAddrMapAdd(newShortAddr, g_zbNIB.ieeeAddr, &addrRef);
	}

	zb_info_save(NULL);
	zdo_device_announce_send();
}

bool zb_NwkAddrConflictDetect(void *arg, uint16_t nwkAddr, addrExt_t ieeeAddr)
{
	addrExt_t extAddr;
	uint16_t addrMapIdx = 0;
	zb_normal_neighbor_entry_t *neighbor;

	if (g_zbNIB.nwkAddr == nwkAddr) {
		if (ZB_IEEE_ADDR_CMP(ieeeAddr, g_zbNIB.ieeeAddr)) {
			return FALSE;
		}

		zb_NwkAddrConflictHandle((zb_buf_t *)arg, nwkAddr, NULL);
		g_sysDiags.nwkAddrConflict++;
		return TRUE;
	}

	neighbor = zb_NeighborTableSearchFromShortAddr(nwkAddr, extAddr, &addrMapIdx);
	if (neighbor == NULL) {
		return FALSE;
	}

	if (ZB_IEEE_ADDR_CMP(ieeeAddr, extAddr) || ZB_IEEE_ADDR_IS_ZERO(extAddr) ||
	    (nwkAddr != 0U)) {
		return FALSE;
	}

	zb_NwkAddrConflictHandle((zb_buf_t *)arg, nwkAddr, neighbor);
	g_sysDiags.nwkAddrConflict++;

	return TRUE;
}

void zb_NwkStatusAddrConflictInd(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	uint16_t conflictAddr;

	COPY_BUFFERTOU16(conflictAddr, buf->buf);

	if (conflictInfo != NULL && addrConflictHandleTimer != NULL) {
		uint16_t pendingConflictAddr;

		COPY_BUFFERTOU16(pendingConflictAddr, conflictInfo->buf);
		if (pendingConflictAddr == conflictAddr) {
			ev_timer_taskCancel(&addrConflictHandleTimer);
			zb_buf_free(conflictInfo);
			conflictInfo = NULL;
		}
	}

	if (conflictAddr == g_zbInfo.macPib.shortAddress && conflictAddr != 0U) {
		uint16_t addrRef = 0;
		uint16_t newShortAddr;

		zb_buf_free(buf);

		newShortAddr = zb_NwkStochasticAddrCal();
		g_zbNIB.nwkAddr = newShortAddr;
		g_zbMacPib.shortAddress = newShortAddr;
		(void)zb_NwkAddrMapAdd(newShortAddr, g_zbInfo.macPib.extAddress, &addrRef);
		zb_info_save(NULL);
		zdo_device_announce_send();
		return;
	}

	nwkRoutingTabEntryDstDel(conflictAddr);
#if defined(ZB_COORDINATOR_ROLE)
	nwkRouteRecTabEntryDstDel(conflictAddr);
#endif

	{
		addrExt_t extAddr;
		uint16_t addrMapIdx = 0;
		zb_normal_neighbor_entry_t *neighbor =
			zb_NeighborTableSearchFromShortAddr(conflictAddr, extAddr, &addrMapIdx);

		if (nwk_neighbor_is_child_end_device(neighbor)) {
			zb_NwkNeighborAddrConflictHandle(buf, neighbor);
			return;
		}
	}

	zb_buf_free(buf);
}
#endif
