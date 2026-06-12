/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK address-conflict detection and resolution.
 *
 * Adapted from libzigbee/src/nwk_addr_conflict.c (~220 LOC). Adaptations:
 *
 *   * vendor "zb_local.h" / "zdo_internal.h" → zb_common_stub.h +
 *     nwk/includes/nwk_internal.h
 *   * forward-declared zdo helpers and NWK command senders that are
 *     still pending other TU ports
 *   * tabs / Zephyr-style formatting; logic preserved verbatim
 */

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_neighbor.h"
#include "os/ev_timer.h"

#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

extern void tl_zbNwkSendNwkStatusCmd(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *pNwkStatus,
				     u8 handle);
extern void tl_zbNwkSendRejoinRespCmd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 ack,
				      u8 handle);
extern void zdo_device_announce_send(void);
extern void zb_buf_clear(zb_buf_t *buf);

zb_buf_t *conflictInfo = NULL;
ev_timer_event_t *addrConflictHandleTimer = NULL;

static inline bool nwk_neighbor_is_child_end_device(const tl_zb_normal_neighbor_entry_t *entry)
{
	return (entry != NULL) &&
	       (entry->deviceType == NWK_DEVICE_TYPE_ED) &&
	       (entry->relationship == NEIGHBOR_IS_CHILD);
}

static void nwk_addrConflictHdrInit(nwk_hdr_t *hdr, u16 dstAddr, bool forceSeqNum)
{
	memset(hdr, 0, sizeof(*hdr));

	hdr->dstAddr = dstAddr;
	hdr->srcAddr = g_zbNIB.nwkAddr;
	hdr->radius = g_zbNIB.maxDepth;
	hdr->seqNum = forceSeqNum ? 0U : g_zbNIB.seqNum++;
	hdr->frameControl.frameType = FRAME_TYPE_COMMAND;
	hdr->frameControl.protocolVer = ZB_PROTOCOL_VERSION;
}

static void nwk_addrConflictStatusSend(zb_buf_t *buf, u16 dstAddr, u16 statusAddr,
				       bool forceSeqNum)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;

	if (buf == NULL) {
		return;
	}

	nwk_addrConflictHdrInit(&hdr, dstAddr, forceSeqNum);
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmdId = NWK_CMD_NETWORK_STATUS;
	cmd.nwkStatus.dstAddr = statusAddr;
	cmd.nwkStatus.statusCode = NWK_COMMAND_STATUS_ADDRESS_CONFLICT;
	tl_zbNwkSendNwkStatusCmd(buf, &hdr, &cmd, NWK_INTERNAL_NETWORK_STATUS_CMD_HANDLE);
}

int nwk_addrConflictCb(void *arg)
{
	ARG_UNUSED(arg);

	if (conflictInfo != NULL) {
		u16 conflictAddr = *(u16 *)conflictInfo;

		zb_buf_clear(conflictInfo);
		nwk_addrConflictStatusSend(conflictInfo, NWK_BROADCAST_RX_ON_WHEN_IDLE,
					   conflictAddr, FALSE);
		conflictInfo = NULL;
	}

	addrConflictHandleTimer = NULL;

	return -1;
}

void tl_zbNwkNeighborAddrConflictHandle(zb_buf_t *buf, tl_zb_normal_neighbor_entry_t *neighbor)
{
	nwk_hdr_t hdr;
	nwkCmd_t cmd;
	addrExt_t extAddr;
	u16 newShortAddr;
	u16 oldShortAddr;

	if (buf == NULL || neighbor == NULL) {
		return;
	}

	newShortAddr = tl_zbNwkStochasticAddrCal();
	oldShortAddr = tl_zbshortAddrByIdx(neighbor->addrmapIdx);
	tl_zbExtAddrByIdx(neighbor->addrmapIdx, extAddr);

	nwk_addrConflictHdrInit(&hdr, oldShortAddr, FALSE);
	if (ss_keyPreconfigured()) {
		hdr.frameControl.security = 1;
	}
	if (memcmp(extAddr, g_zero_addr, EXT_ADDR_LEN) != 0) {
		hdr.frameControl.dstIEEEAddr = 1;
		memcpy(hdr.dstIeeeAddr, extAddr, EXT_ADDR_LEN);
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmdId = NWK_CMD_REJOIN_RESPONSE;
	cmd.rejoinRsp.nwkAddr = newShortAddr;
	cmd.rejoinRsp.rejoinStatus = MAC_SUCCESS;
	tl_zbNwkSendRejoinRespCmd(buf, &hdr, &cmd, neighbor->rxOnWhileIdle,
				  NWK_INTERNAL_REJOIN_RESP_CMD_HANDLE);

	g_nwkAddrMap.addrMap[neighbor->addrmapIdx].shortAddr = newShortAddr;
}

void tl_zbNwkAddrConflictStatusSend(u16 dstAddr, u16 statusAddr, u8 forceSeqNum)
{
	zb_buf_t *buf = zb_buf_allocate();

	if (buf != NULL) {
		nwk_addrConflictStatusSend(buf, dstAddr, statusAddr, forceSeqNum != 0U);
	}
}

void tl_zbNwkAddrConflictHandle(zb_buf_t *buf, u16 nwkAddr,
				tl_zb_normal_neighbor_entry_t *neighbor)
{
	if (addrConflictHandleTimer != NULL) {
		ev_timer_taskCancel(&addrConflictHandleTimer);
	}

	if (conflictInfo != NULL) {
		zb_buf_free(conflictInfo);
	}

	conflictInfo = buf;
	*(u16 *)buf = nwkAddr;
	addrConflictHandleTimer =
		ev_timer_taskPost(nwk_addrConflictCb, buf, (drv_u32Rand() & 0x3fU) + 1U);

	if (neighbor != NULL) {
		if (nwk_neighbor_is_child_end_device(neighbor)) {
			zb_buf_t *rejoinBuf = zb_buf_allocate();

			if (rejoinBuf != NULL) {
				tl_zbNwkNeighborAddrConflictHandle(rejoinBuf, neighbor);
			}
		}

		return;
	}

	{
		u16 addrRef = 0;
		u16 newShortAddr = tl_zbNwkStochasticAddrCal();

		g_zbNIB.nwkAddr = newShortAddr;
		g_zbMacPib.shortAddress = newShortAddr;
		(void)tl_zbNwkAddrMapAdd(newShortAddr, g_zbNIB.ieeeAddr, &addrRef);
	}

	zb_info_save(NULL);
	zdo_device_announce_send();
}

bool tl_zbNwkAddrConflictDetect(void *arg, u16 nwkAddr, addrExt_t ieeeAddr)
{
	addrExt_t extAddr;
	u16 addrMapIdx = 0;
	tl_zb_normal_neighbor_entry_t *neighbor;

	if (g_zbNIB.nwkAddr == nwkAddr) {
		if (memcmp(ieeeAddr, g_zbNIB.ieeeAddr, EXT_ADDR_LEN) != 0) {
			tl_zbNwkAddrConflictHandle((zb_buf_t *)arg, nwkAddr, NULL);
			g_sysDiags.nwkAddrConflict++;
			return TRUE;
		}
	}

	neighbor = tl_zbNeighborTableSearchFromShortAddr(nwkAddr, extAddr, &addrMapIdx);
	if (neighbor == NULL) {
		return FALSE;
	}

	if (memcmp(ieeeAddr, extAddr, EXT_ADDR_LEN) == 0 ||
	    memcmp(extAddr, g_zero_addr, EXT_ADDR_LEN) == 0 ||
	    (nwkAddr != 0U)) {
		return FALSE;
	}

	tl_zbNwkAddrConflictHandle((zb_buf_t *)arg, nwkAddr, neighbor);
	g_sysDiags.nwkAddrConflict++;

	return TRUE;
}

void tl_zbNwkStatusAddrConflictInd(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)arg;
	u16 conflictAddr = *(u16 *)buf;

	if (conflictInfo != NULL && addrConflictHandleTimer != NULL) {
		if (*(u16 *)conflictInfo == conflictAddr) {
			ev_timer_taskCancel(&addrConflictHandleTimer);
			zb_buf_free(conflictInfo);
			conflictInfo = NULL;
		}
	}

	if (conflictAddr == g_zbNIB.nwkAddr && conflictAddr != 0U) {
		u16 addrRef = 0;
		u16 newShortAddr;

		zb_buf_free(buf);

		newShortAddr = tl_zbNwkStochasticAddrCal();
		g_zbNIB.nwkAddr = newShortAddr;
		g_zbMacPib.shortAddress = newShortAddr;
		(void)tl_zbNwkAddrMapAdd(newShortAddr, g_zbNIB.ieeeAddr, &addrRef);
		zb_info_save(NULL);
		zdo_device_announce_send();
		return;
	}

	nwkRoutingTabEntryDstDel(conflictAddr);

	{
		addrExt_t extAddr;
		u16 addrMapIdx = 0;
		tl_zb_normal_neighbor_entry_t *neighbor =
			tl_zbNeighborTableSearchFromShortAddr(conflictAddr, extAddr, &addrMapIdx);

		if (nwk_neighbor_is_child_end_device(neighbor)) {
			tl_zbNwkNeighborAddrConflictHandle(buf, neighbor);
			return;
		}
	}

	zb_buf_free(buf);
}

#endif /* ZB_ROUTER_ROLE */
