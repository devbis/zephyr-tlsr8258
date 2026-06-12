/* SPDX-License-Identifier: Apache-2.0 */
/*
 * NWK broadcast-retransmit table for the router build.
 *
 * Adapted from libzigbee/src/nwk_brc.c (~280 LOC). Kept structurally
 * one-for-one with the vendor file; adaptations:
 *
 *   * vendor "zb_local.h" → zb_common_stub.h + os/ev_timer.h
 *   * NWK_BRC_JITTER / NWK_BRC_TRANSTBL_SIZE / NWK_BRC_PASSIVE_ACK_ENABLE /
 *     g_brcTransTab[] storage is in subsys/zigbee/common/zb_config.c
 *     (SDK copy). The two extra runtime counters (g_brcTransJitter,
 *     g_brcTransTabCnt) live here as in the vendor file.
 *   * getPassiveAckTimeout() is inlined from libzigbee/src/nwk.c so we
 *     don't need to port that whole TU just for one accessor.
 *   * tl_zbNeighborTabSearchForChildEndDev() and
 *     tl_zbNeighborTableRouterValidNumGet() are forward-declared; the
 *     neighbor table port follows in a later commit.
 *
 * The table is only meaningful once the NWK forwarding/relay path is
 * wired, which still depends on MAC MLME-START support. For now this
 * compiles and is dead-stripped by --gc-sections; landing it early
 * means subsequent NWK forwarding work has a real broadcast record
 * table to drive.
 */

#include "zb_common_stub.h"
#include "os/ev_timer.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"

#include <stdbool.h>
#include <string.h>

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

/* Forward declarations for symbols defined in nwk_neighbor.c (still
 * pending port from libzigbee/src/nwk_neighbor.c).
 */
extern tl_zb_normal_neighbor_entry_t *tl_zbNeighborTabSearchForChildEndDev(void *entry);

u32 g_brcTransJitter = NWK_MAX_BROADCAST_JITTER;
u8 g_brcTransTabCnt;

static inline u32 getPassiveAckTimeout(void)
{
	return g_zbNIB.passiveAckTimeout ? g_zbNIB.passiveAckTimeout
					 : NWK_PASSIVE_ACK_TIMEOUT;
}

static inline bool nwk_brc_entry_used(const nwk_brcTransRecordEntry_t *entry)
{
	return entry != NULL && entry->used != 0U;
}

void nwkBrcMsgAllDevicesCb(void *arg)
{
	nwk_brcTransRecordEntry_t *entry = (nwk_brcTransRecordEntry_t *)arg;

	if (entry == NULL || entry->entry == NULL || entry->edEntry == NULL) {
		return;
	}
	entry->status = NWK_STATUS_SUCCESS;
}

void nwkMsgSendCb(void *arg)
{
	nwk_brcTransRecordEntry_t *entry = (nwk_brcTransRecordEntry_t *)arg;

	if (entry != NULL) {
		entry->status = NWK_STATUS_SUCCESS;
	}
}

int nwkMsgSendCbDelay(void *arg)
{
	nwkMsgSendCb(arg);
	return -1;
}

void nwkBrcTransJitterSet(void)
{
	g_brcTransJitter = (u32)(drv_u32Rand() % (NWK_BRC_JITTER + 1U));
}

void nwkBrcTransTabEntryRst(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	if (entry->retryTimer != NULL) {
		ev_timer_taskCancel(&entry->retryTimer);
	}
	if (entry->passiveAckAddr != NULL) {
		ev_buf_free((u8 *)entry->passiveAckAddr);
	}

	memset(entry, 0, sizeof(*entry));
}

void nwkBrcTransTabEntryClear(nwk_brcTransRecordEntry_t *entry)
{
	if (!nwk_brc_entry_used(entry)) {
		return;
	}

	nwkBrcTransTabEntryRst(entry);
	if (g_brcTransTabCnt != 0U) {
		g_brcTransTabCnt--;
	}
}

void nwkBrcTransTabInit(void)
{
	g_brcTransTabCnt = 0;
	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		nwkBrcTransTabEntryRst(&g_brcTransTab[i]);
	}
}

nwk_brcTransRecordEntry_t *nwkBrcTransEntryFind(u16 srcAddr, u8 seqNum)
{
	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		nwk_brcTransRecordEntry_t *entry = &g_brcTransTab[i];

		if (nwk_brc_entry_used(entry) && entry->srcAddr == srcAddr &&
		    entry->seqNum == seqNum) {
			return entry;
		}
	}

	return NULL;
}

nwk_brcTransRecordEntry_t *nwkBrcTransEntryCreate(nwk_txDataPendEntry_t *pend,
						  u16 srcAddr, u8 seqNum)
{
	nwk_brcTransRecordEntry_t *entry = nwkBrcTransEntryFind(srcAddr, seqNum);

	if (entry != NULL) {
		return entry;
	}

	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		if (!g_brcTransTab[i].used) {
			entry = &g_brcTransTab[i];
			break;
		}
	}

	if (entry == NULL) {
		entry = &g_brcTransTab[0];
		nwkBrcTransTabEntryClear(entry);
	}

	memset(entry, 0, sizeof(*entry));
	entry->entry = pend;
	entry->srcAddr = srcAddr;
	entry->seqNum = seqNum;
	entry->expirationTime = g_zbNIB.maxBroadcastRetries + 1U;
	entry->status = NWK_STATUS_SUCCESS;
	entry->used = 1;
	g_brcTransTabCnt++;

	if (NWK_BRC_PASSIVE_ACK_ENABLE) {
		entry->passiveAckAddr =
			(u16 *)ev_buf_allocate((u16)(sizeof(u16) * TL_ZB_NEIGHBOR_TABLE_SIZE));
	}

	return entry;
}

u8 nwkBrcMsgAddSender(nwk_brcTransRecordEntry_t *entry, u16 shortAddr)
{
	if (entry == NULL || entry->passiveAckAddr == NULL) {
		return 0;
	}

	entry->passiveAckAddr[entry->activeNum++] = shortAddr;
	return 1;
}

u8 nwkBrcAckFind(nwk_brcTransRecordEntry_t *entry, u16 shortAddr)
{
	if (entry == NULL || entry->passiveAckAddr == NULL) {
		return 0;
	}

	for (u8 i = 0; i < entry->activeNum; i++) {
		if (entry->passiveAckAddr[i] == shortAddr) {
			return 1;
		}
	}

	return 0;
}

u8 nwkBrcAllRelayed(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL) {
		return 1;
	}

	return (entry->activeNum >= tl_zbNeighborTableRouterValidNumGet()) ? 1U : 0U;
}

int nwkBrcMsgPassiveAckTimeoutCb(void *arg)
{
	nwk_brcTransRecordEntry_t *entry = (nwk_brcTransRecordEntry_t *)arg;

	if (entry == NULL) {
		return -1;
	}

	if (nwkBrcAllRelayed(entry) || entry->retries >= g_zbNIB.maxBroadcastRetries) {
		nwkBrcTransTabEntryClear(entry);
		return -1;
	}

	entry->retries++;
	return (int)getPassiveAckTimeout();
}

ev_timer_event_t *nwkBrcMsgPassiveAckTimeoutStart(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL) {
		return NULL;
	}

	entry->retryTimer = ev_timer_taskPost(nwkBrcMsgPassiveAckTimeoutCb, entry,
					      getPassiveAckTimeout());
	return entry->retryTimer;
}

void nwkBrcMsgAllEndDevStart(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	for (tl_zb_normal_neighbor_entry_t *child = NULL;
	     (child = tl_zbNeighborTabSearchForChildEndDev(child)) != NULL;) {
		entry->edEntry = child;
		nwkBrcMsgAllDevicesCb(entry);
	}
}

void nwkBrcTimerStart(nwk_brcTransRecordEntry_t *entry)
{
	if (entry == NULL) {
		return;
	}

	nwkBrcTransJitterSet();
	if (entry->retryTimer != NULL) {
		ev_timer_taskCancel(&entry->retryTimer);
	}
	entry->retryTimer = ev_timer_taskPost(nwkMsgSendCbDelay, entry, g_brcTransJitter);
}

int nwkBrcPeriodic(void *arg)
{
	ARG_UNUSED(arg);

	for (u8 i = 0; i < NWK_BRC_TRANSTBL_SIZE; i++) {
		nwk_brcTransRecordEntry_t *entry = &g_brcTransTab[i];

		if (!nwk_brc_entry_used(entry)) {
			continue;
		}

		if (entry->expirationTime != 0U) {
			entry->expirationTime--;
		}

		if (entry->expirationTime == 0U) {
			nwkBrcTransTabEntryClear(entry);
		}
	}

	return 0;
}

u8 nwkBrcCheckDevMatch(u16 dstAddr)
{
	if (dstAddr == NWK_BROADCAST_RX_ON_WHEN_IDLE) {
		return ZB_PIB_RX_ON_WHEN_IDLE() ? 1U : 0U;
	}

	if (dstAddr == NWK_BROADCAST_ALL_DEVICES) {
		return 1U;
	}

	if (dstAddr == NWK_BROADCAST_LOW_POWER_ROUTER) {
		return 0U;
	}

	return 1U;
}

#else /* !ZB_ROUTER_ROLE */

u8 nwkBrcCheckDevMatch(u16 dstAddr)
{
	if (dstAddr == NWK_BROADCAST_RX_ON_WHEN_IDLE) {
		return ZB_PIB_RX_ON_WHEN_IDLE() ? 1U : 0U;
	}

	if (dstAddr == NWK_BROADCAST_ALL_DEVICES) {
		return 1U;
	}

	if (dstAddr == NWK_BROADCAST_LOW_POWER_ROUTER) {
		return 0U;
	}

	return 1U;
}

#endif /* ZB_ROUTER_ROLE */
