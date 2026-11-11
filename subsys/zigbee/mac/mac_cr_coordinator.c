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
	uint8_t *payload;
	uint8_t len;
} mac_packet_delay_ctx_t;

typedef struct _attribute_packed_ {
	addrExt_t orphanAddr;
	uint16_t shortAddr;
	uint8_t pad; /* offset 10, unused */
} mac_orphan_saved_t;

typedef struct _attribute_packed_ {
	uint8_t headPad[2]; /* offsets 0-1, left as the buffer held them */
	addrExt_t srcAddr;
	uint8_t srcAddrMode;
	uint16_t dstShortAddr;
	uint8_t midPad[6]; /* offsets 13-18, left as the buffer held them */
	uint8_t dstAddrMode;
	uint8_t status;
	uint8_t isAssoc;
} mac_orphan_comm_status_buf_t;

STATIC_ASSERT(sizeof(mac_orphan_saved_t) == 11);
STATIC_ASSERT(sizeof(mac_orphan_comm_status_buf_t) == 22);

enum {
	MAC_COORD_REALIGN_EXT_DST_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND | MAC_FCF_ACK_REQUEST_MASK |
		((uint16_t)ADDR_MODE_EXT << MAC_FCF_DST_ADDR_MODE_POS) |
		((uint16_t)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS),
	MAC_COORD_REALIGN_SHORT_DST_FRAME_CTRL = MAC_FRAME_TYPE_COMMAND |
		((uint16_t)ADDR_MODE_SHORT << MAC_FCF_DST_ADDR_MODE_POS) |
		((uint16_t)ADDR_MODE_EXT << MAC_FCF_SRC_ADDR_MODE_POS),
};

uint8_t zb_MacMlmeBeaconCmdSend(zb_BeaconFrame_t *beacon)
{
	zb_mac_mhr_t mhr;
	zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
	uint8_t *payload;
	uint8_t len;

	memset(&mhr, 0, sizeof(mhr));
	mhr.srcPanId = g_zbMacPib.panId;
	if (beacon->srcAddrMode == ADDR_MODE_EXT) {
		ZB_IEEE_ADDR_COPY(&mhr.srcAddr, g_zbMacPib.extAddress);
	} else {
		memcpy(&mhr.srcAddr, &g_zbMacPib.shortAddress, SHORT_ADDR_LEN);
	}

	/* Frame type beacon (0),
	 * no PAN ID compression, so the header carries the source PAN ID. */
	mhr.frameCtrl = (uint16_t)(((uint16_t)beacon->srcAddrMode << 14) | ((uint16_t)beacon->framePending << 4));
	len = (uint8_t)(zb_MacHdrSize(mhr.frameCtrl) + g_zbMacPib.beaconPayloadLen + 4U);

	if (buf->hdr.active != 0U) {
		return MAC_SUCCESS;
	}

	buf->hdr.active = 1;

	ZB_BUF_INITIAL_ALLOC(buf, len, payload, uint8_t *);
	{
		mac_packet_delay_ctx_t ctx;

		ctx.payload = payload;
		ctx.len = len;
		memcpy(buf->buf, &ctx, sizeof(ctx));
	}
	memset(payload, 0, len);

	payload = zb_MacHdrBuilder(payload, &mhr);
	payload[0] = (uint8_t)((((uint16_t)beacon->superframeOrder) << 4) | (beacon->beaconOrder & 0x0fU));
	payload[1] =
		(uint8_t)(0x0fU | ((beacon->ble & 0x01U) << 4) | (g_zbMacPib.associationPermit << 7));
#if defined(ZB_COORDINATOR_ROLE)
	/* A designated coordinator marks the superframe
	 * specification as a PAN coordinator and then falls into the same beacon
	 * payload copy.  Treating the two as alternatives sent a coordinator
	 * beacon with no network payload, which no joiner accepts. */
	if (aps_ib.aps_designated_coordinator) {
		payload[1] |= 0x40U;
	}
#endif
	memcpy(payload + 4, &g_zbMacPib.beaconPayload, g_zbMacPib.beaconPayloadLen);

	ev_timer_taskPost(zb_MacPacketDelaySend, NULL, (drv_u32Rand() & 0x14U) + 1U);

	return MAC_SUCCESS;
}

uint8_t zb_MacMlmeCoordRealignmentCmdSend(uint8_t rxOnWhenIdle, const uint8_t *orphanAddr, uint16_t shortAddr,
				       void *arg)
{
	/* Raw MHR: frame control at 0, destination PAN at 6, destination address
	 * at 8, source PAN at 16, source extended address at 18. */
	uint8_t mhrBuf[26];
	uint8_t realignCmd[9];
	uint8_t *psdu;
	uint8_t *payload;
	uint8_t psduLen;
	uint8_t ack;
	uint16_t frameCtrl;

	memset(mhrBuf, 0, sizeof(mhrBuf));
	memset(realignCmd, 0, sizeof(realignCmd));

	COPY_U16TOBUFFER(mhrBuf + 6, MAC_SHORT_ADDR_NONE);
	COPY_U16TOBUFFER(mhrBuf + 16, g_zbInfo.macPib.panId);
	COPY_U16TOBUFFER(mhrBuf + 8, MAC_SHORT_ADDR_NONE);
	ZB_IEEE_ADDR_COPY(mhrBuf + 18, g_zbInfo.macPib.extAddress);

	if (rxOnWhenIdle == 0U) {
		ZB_IEEE_ADDR_COPY(mhrBuf + 8, orphanAddr);
		COPY_U16TOBUFFER(mhrBuf, MAC_COORD_REALIGN_EXT_DST_FRAME_CTRL);
		psduLen = (uint8_t)(zb_MacHdrSize(MAC_COORD_REALIGN_EXT_DST_FRAME_CTRL) + 9U);
		realignCmd[0] = MAC_CMD_COORDINATOR_REALIGNMENT;
		memcpy(realignCmd + 1, (uint8_t *)arg + 4, 2);
		COPY_U16TOBUFFER(realignCmd + 3, g_zbInfo.macPib.shortAddress);
		realignCmd[5] = ((uint8_t *)arg)[6];
		COPY_U16TOBUFFER(realignCmd + 6, shortAddr);
		realignCmd[8] = ((uint8_t *)arg)[7];
		ack = 1U;
	} else {
		COPY_U16TOBUFFER(mhrBuf, MAC_COORD_REALIGN_SHORT_DST_FRAME_CTRL);
		psduLen = (uint8_t)(zb_MacHdrSize(MAC_COORD_REALIGN_SHORT_DST_FRAME_CTRL) + 9U);
		realignCmd[0] = MAC_CMD_COORDINATOR_REALIGNMENT;
		memcpy(realignCmd + 1, (uint8_t *)arg + 4, 2);
		COPY_U16TOBUFFER(realignCmd + 3, g_zbInfo.macPib.shortAddress);
		realignCmd[5] = ((uint8_t *)arg)[6];
		COPY_U16TOBUFFER(realignCmd + 6, MAC_SHORT_ADDR_NONE);
		realignCmd[8] = ((uint8_t *)arg)[7];
		ack = 0U;
	}

	ZB_BUF_INITIAL_ALLOC((zb_buf_t *)arg, psduLen, psdu, uint8_t *);
	payload = zb_MacHdrBuilder(psdu, (zb_mac_mhr_t *)mhrBuf);
	memcpy(payload, realignCmd, sizeof(realignCmd));

	COPY_BUFFERTOU16(frameCtrl, mhrBuf);
	if ((frameCtrl & MAC_FCF_FRAME_VERSION_MASK) == 0U) {
		psduLen--;
	}

	return zb_MacTx((zb_buf_t *)arg, psdu, psduLen, ack, NULL);
}

void zb_MacBeaconRequestCb(void)
{
	zb_BeaconFrame_t beacon;

	if (g_zbNIB.capabilityInfo.devType == 0U || g_zbMacPib.beaconPayloadLen == 0U) {
		return;
	}

	zb_NwkBeaconPayloadUpdate();

	memset(&beacon, 0, sizeof(beacon));
	beacon.srcAddrMode = ADDR_MODE_SHORT;
	beacon.ble = g_zbMacPib.battLifeExt;
	beacon.beaconOrder = g_zbMacPib.beaconOrder;
	beacon.superframeOrder = g_zbMacPib.superframeOrder;
	zb_MacMlmeBeaconCmdSend(&beacon);
	g_zbMacCtx.beaconTriesNum--;
}

int zb_MacPacketDelaySend(void *arg)
{
	zb_buf_t *buf = (zb_buf_t *)g_zbMacCtx.txRawDataBuf;
	mac_packet_delay_ctx_t ctx;
	uint8_t *payload;
	uint8_t len;
	uint8_t status;

	(void)arg;

	buf->hdr.handle = MAC_INTERNAL_BEACON_SEND_HANDLE;
	memcpy(&ctx, buf->buf, sizeof(ctx));
	payload = ctx.payload;
	len = ctx.len;
	status = zb_MacTx(buf, payload, len, 0, NULL);

	return (status == MAC_SUCCESS) ? -1 : 0;
}

void zb_MacOrphanResponseHandler(void *arg)
{
	mac_orphan_saved_t saved;
	mac_orphan_saved_t *req = (mac_orphan_saved_t *)arg;
	uint8_t status;

	memset(&saved, 0, sizeof(saved));
	memcpy(&saved, arg, sizeof(saved));
	memcpy((uint8_t *)arg + 4, &g_zbInfo.macPib.panId, sizeof(g_zbInfo.macPib.panId));
	((uint8_t *)arg)[6] = g_zbMacCtx.curChannel;
	((uint8_t *)arg)[7] = 0;
	((zb_buf_t *)arg)->hdr.handle = MAC_INTERNAL_ORPHAN_RESPONSE_HANDLE;

	status = zb_MacMlmeCoordRealignmentCmdSend(0, saved.orphanAddr, req->shortAddr, arg);
	if (status != MAC_SUCCESS) {
		zb_MacOrphanResponseStatusCheck(arg, MAC_STA_TRANSACTION_OVERFLOW);
	}
}

void zb_MacOrphanResponseStatusCheck(void *arg, uint8_t status)
{
	mac_orphan_saved_t saved;
	mac_orphan_comm_status_buf_t *ind = (mac_orphan_comm_status_buf_t *)arg;

	memcpy(&saved, arg, sizeof(saved));
	if (status != MAC_SUCCESS) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	/* Only status and isAssoc are cleared; headPad and midPad keep whatever
	 * the buffer held. */
	ind->status = MAC_SUCCESS;
	ind->isAssoc = 0;
	memcpy(ind->srcAddr, saved.orphanAddr, sizeof(ind->srcAddr));
	ind->srcAddrMode = ADDR_MODE_EXT;
	ind->dstShortAddr = saved.shortAddr;
	ind->dstAddrMode = ADDR_MODE_SHORT;
	zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_COMM_STATUS_IND, arg);
}

void zb_MacMlmeBeaconSendConfirm(void *arg, uint8_t status)
{
	(void)arg;

	if (status != MAC_SUCCESS && g_zbMacCtx.beaconTriesNum != 0U) {
		zb_MacBeaconRequestCb();
	}
}
#endif
