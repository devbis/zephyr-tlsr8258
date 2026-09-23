/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "ev_timer.h"
#include "mac.h"
#include "mac_common.h"
#include "mac_cr_coordinator.h"
#include "mac_data.h"
#include "mac_indirect_data.h"
#include "mac_trx.h"
#include "nwk_data.h"

#if defined(ZB_ROUTER_ROLE)
typedef struct {
	u8 *payload;
	u8 len;
} mac_packet_delay_ctx_t;

typedef struct _attribute_packed_ {
	u8 bytes0_7[8];
	u16 shortAddr;
	u8 reserved10;
} mac_orphan_saved_t;

typedef struct _attribute_packed_ {
	u8 reserved0[2];
	u8 srcAddrBytes[8];
	u8 srcAddrMode;
	u16 dstShortAddr;
	u8 reserved13[6];
	u8 dstAddrMode;
	u8 status;
	u8 isAssoc;
} mac_orphan_comm_status_buf_t;

STATIC_ASSERT(sizeof(mac_orphan_saved_t) == 11);
STATIC_ASSERT(sizeof(mac_orphan_comm_status_buf_t) == 22);

enum {
	MAC_COORD_REALIGN_EXT_DST_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK |
		((u16)ADDR_MODE_EXT << MAC_FCF_DST_ADDR_MODE_POS) |
		((u16)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS),
	MAC_COORD_REALIGN_SHORT_DST_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND |
		((u16)ADDR_MODE_SHORT << MAC_FCF_DST_ADDR_MODE_POS) |
		((u16)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS),
};

u8 tl_zbMacMlmeBeaconCmdSend(tl_zbBeaconFrame_t *beacon)
{
	tl_zb_mac_mhr_t mhr;
	zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
	u8 *payload;
	u8 len;

	memset(&mhr, 0, sizeof(mhr));
	mhr.srcPanId = g_zbMacPib.panId;
	if (beacon->srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(&mhr.srcAddr, g_zbMacPib.extAddress);
	} else {
		memcpy(&mhr.srcAddr, &g_zbMacPib.shortAddress, SHORT_ADDR_LEN);
	}

	mhr.frameCtrl = (u16)(0x0043U | ((u16)beacon->srcAddrMode << 14) |
			      ((u16)beacon->framePending << 4));
	len = (u8)(tl_zbMacHdrSize(mhr.frameCtrl) + g_zbMacPib.beaconPayloadLen + 4U);

	if (buf->hdr.active != 0U) {
		return MAC_SUCCESS;
	}

	buf->hdr.active = 1;

	TL_BUF_INITIAL_ALLOC(buf, len, payload, u8 *);
	{
		mac_packet_delay_ctx_t ctx;

		ctx.payload = payload;
		ctx.len = len;
		memcpy(buf->buf, &ctx, sizeof(ctx));
	}
	memset(payload, 0, len);

	payload = tl_zbMacHdrBuilder(payload, &mhr);
	payload[0] = (u8)((((u16)beacon->superframeOrder) << 4) | (beacon->beaconOrder & 0x0fU));
	payload[1] =
		(u8)(0x0fU | ((beacon->ble & 0x01U) << 4) | (g_zbMacPib.associationPermit << 7));
#if defined(ZB_COORDINATOR_ROLE)
	/* "102: tcmp r1,#1; 104: tjeq 12a" then "12a: tmovs r1,#64; 12c: tors
	 * r3,r1 ... 130: tj 106" - a designated coordinator marks the superframe
	 * specification as a PAN coordinator and then falls into the same beacon
	 * payload copy.  Treating the two as alternatives sent a coordinator
	 * beacon with no network payload, which no joiner accepts. */
	if (aps_ib.aps_designated_coordinator) {
		payload[1] |= 0x40U;
	}
#endif
	memcpy(payload + 4, &g_zbMacPib.beaconPayload, g_zbMacPib.beaconPayloadLen);

	ev_timer_taskPost(tl_zbMacPacketDelaySend, NULL, (drv_u32Rand() & 0x14U) + 1U);

	return MAC_SUCCESS;
}

u8 tl_zbMacMlmeCoordRealignmentCmdSend(u8 rxOnWhenIdle, const u8 *orphanAddr, u16 shortAddr,
				       void *arg)
{
	u8 rawMhr[26];
	u8 cmd[9];
	u8 *psdu;
	u8 *payload;
	u8 psduLen;
	u8 ack;
	u16 frameCtrl;

	memset(rawMhr, 0, sizeof(rawMhr));
	memset(cmd, 0, sizeof(cmd));

	COPY_U16TOBUFFER(rawMhr + 6, MAC_SHORT_ADDR_NONE);
	COPY_U16TOBUFFER(rawMhr + 16, g_zbInfo.macPib.panId);
	COPY_U16TOBUFFER(rawMhr + 8, MAC_SHORT_ADDR_NONE);
	ZB_IEEE_ADDR_COPY(rawMhr + 18, g_zbInfo.macPib.extAddress);

	if (rxOnWhenIdle == 0U) {
		ZB_IEEE_ADDR_COPY(rawMhr + 8, orphanAddr);
		COPY_U16TOBUFFER(rawMhr, MAC_COORD_REALIGN_EXT_DST_FRAME_CTRL);
		psduLen = (u8)(tl_zbMacHdrSize(MAC_COORD_REALIGN_EXT_DST_FRAME_CTRL) + 9U);
		cmd[0] = MAC_CMD_COORDINATOR_REALIGNMENT;
		memcpy(cmd + 1, (u8 *)arg + 4, 2);
		COPY_U16TOBUFFER(cmd + 3, g_zbInfo.macPib.shortAddress);
		cmd[5] = ((u8 *)arg)[6];
		COPY_U16TOBUFFER(cmd + 6, shortAddr);
		cmd[8] = ((u8 *)arg)[7];
		ack = 1U;
	} else {
		COPY_U16TOBUFFER(rawMhr, MAC_COORD_REALIGN_SHORT_DST_FRAME_CTRL);
		psduLen = (u8)(tl_zbMacHdrSize(MAC_COORD_REALIGN_SHORT_DST_FRAME_CTRL) + 9U);
		cmd[0] = MAC_CMD_COORDINATOR_REALIGNMENT;
		memcpy(cmd + 1, (u8 *)arg + 4, 2);
		COPY_U16TOBUFFER(cmd + 3, g_zbInfo.macPib.shortAddress);
		cmd[5] = ((u8 *)arg)[6];
		COPY_U16TOBUFFER(cmd + 6, MAC_SHORT_ADDR_NONE);
		cmd[8] = ((u8 *)arg)[7];
		ack = 0U;
	}

	TL_BUF_INITIAL_ALLOC((zb_buf_t *)arg, psduLen, psdu, u8 *);
	payload = tl_zbMacHdrBuilder(psdu, (tl_zb_mac_mhr_t *)rawMhr);
	memcpy(payload, cmd, sizeof(cmd));

	COPY_BUFFERTOU16(frameCtrl, rawMhr);
	if ((frameCtrl & MAC_FCF_FRAME_VERSION_MASK) == 0U) {
		psduLen--;
	}

	return tl_zbMacTx((zb_buf_t *)arg, psdu, psduLen, ack, NULL);
}

void tl_zbMacBeaconRequestCb(void)
{
	tl_zbBeaconFrame_t beacon;

	if (g_zbNIB.capabilityInfo.devType == 0U || g_zbMacPib.beaconPayloadLen == 0U) {
		return;
	}

	tl_zbNwkBeaconPayloadUpdate();

	memset(&beacon, 0, sizeof(beacon));
	beacon.srcAddrMode = ADDR_MODE_SHORT;
	beacon.ble = g_zbMacPib.battLifeExt;
	beacon.beaconOrder = g_zbMacPib.beaconOrder;
	beacon.superframeOrder = g_zbMacPib.superframeOrder;
	tl_zbMacMlmeBeaconCmdSend(&beacon);
	g_zbMacCtx.beaconTriesNum--;
}

int tl_zbMacPacketDelaySend(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
	mac_packet_delay_ctx_t ctx;
	u8 *payload;
	u8 len;
	u8 status;

	(void)arg;

	buf->hdr.handle = MAC_INTERNAL_BEACON_SEND_HANDLE;
	memcpy(&ctx, buf->buf, sizeof(ctx));
	payload = ctx.payload;
	len = ctx.len;
	status = tl_zbMacTx(buf, payload, len, 0, NULL);

	return (status == MAC_SUCCESS) ? -1 : 0;
}

void tl_zbMacOrphanResponseHandler(void *arg)
{
	mac_orphan_saved_t saved;
	mac_orphan_saved_t *req = (mac_orphan_saved_t *)arg;
	u8 status;

	memset(&saved, 0, sizeof(saved));
	memcpy(&saved, arg, sizeof(saved));
	memcpy((u8 *)arg + 4, &g_zbInfo.macPib.panId, sizeof(g_zbInfo.macPib.panId));
	((u8 *)arg)[6] = g_zbMacCtx.curChannel;
	((u8 *)arg)[7] = 0;
	((zb_buf_t *)arg)->hdr.handle = MAC_INTERNAL_ORPHAN_RESPONSE_HANDLE;

	status = tl_zbMacMlmeCoordRealignmentCmdSend(0, saved.bytes0_7, req->shortAddr, arg);
	if (status != MAC_SUCCESS) {
		tl_zbMacOrphanResponseStatusCheck(arg, MAC_STA_TRANSACTION_OVERFLOW);
	}
}

void tl_zbMacOrphanResponseStatusCheck(void *arg, u8 status)
{
	mac_orphan_saved_t saved;
	mac_orphan_comm_status_buf_t *ind = (mac_orphan_comm_status_buf_t *)arg;

	memcpy(&saved, arg, sizeof(saved));
	if (status != MAC_SUCCESS) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	/* The vendor library only clears the two trailing fields
	 * ("22: tstorerb r5,[r4,#20]; 24: tstorerb r5,[r4,#21]"); reserved0 and
	 * reserved13 keep whatever the buffer held. */
	ind->status = MAC_SUCCESS;
	ind->isAssoc = 0;
	memcpy(ind->srcAddrBytes, saved.bytes0_7, sizeof(ind->srcAddrBytes));
	ind->srcAddrMode = ADDR_MODE_EXT;
	ind->dstShortAddr = saved.shortAddr;
	ind->dstAddrMode = ADDR_MODE_SHORT;
	tl_zbPrimitivePost(TL_Q_MAC2NWK, MAC_MLME_COMM_STATUS_IND, arg);
}

void tl_zbMacMlmeBeaconSendConfirm(void *arg, u8 status)
{
	(void)arg;

	if (status != MAC_SUCCESS && g_zbMacCtx.beaconTriesNum != 0U) {
		tl_zbMacBeaconRequestCb();
	}
}
#endif
