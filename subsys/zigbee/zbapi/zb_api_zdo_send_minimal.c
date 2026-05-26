/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include "zb_common_stub.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "zb_minimal_ccm.h"

LOG_MODULE_REGISTER(zigbee_zdo_tx_minimal, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZB_MINIMAL_ZDO_TX_RETRIES          4U
#define ZB_MINIMAL_ZDO_TX_RETRY_DELAY_US   5000U
#define ZB_MINIMAL_APS_MIC_LEN             4U
#define ZB_MINIMAL_APS_CCM_L_VAL           2U
#define ZB_MINIMAL_NWK_MIC_LEN             4U
#define ZB_MINIMAL_NWK_SEC_CTRL            0x2DU
#define ZB_MINIMAL_NWK_SEC_CTRL_WIRE       0x28U
#define ZB_MINIMAL_NWK_AUX_HDR_LEN         14U

extern bool tl_zbNwkEdMinimalParentCandidateGet(u16 *parentShortAddr, addrExt_t parentIeeeAddr);
extern bool tl_zbNwkEdMinimalCanProcessDataFrames(void);
extern u8 ss_keyHash(u8 *padV, u8 *key, u8 *hashOut);

static u8 g_minimal_aps_counter;
static u8 g_minimal_zdp_seq;

#define ZB_MINIMAL_ZDP_CB_MAX 4U

typedef struct {
	zdo_callback cb;
	u16 seq;
	u8 used;
	u8 active;
} zb_minimal_zdp_cb_info_t;

static zb_minimal_zdp_cb_info_t g_minimal_zdp_cbs[ZB_MINIMAL_ZDP_CB_MAX];
static u8 g_minimal_zdp_cb_wptr;
u32 zb_request_key_trace[4] = {0x41505251U, 0U, 0U, 0U};

static u8 ss_apsEnAuxHdrFill(u8 *auxHdr, void *keyInfo, u8 extNonceOpt)
{
	u8 *p = auxHdr + 5U;
	u8 *key = (u8 *)keyInfo;

	COPY_U32TOBUFFER(auxHdr + 1, ss_ib.outgoingFrameCounter);
	ss_ib.outgoingFrameCounter++;

	auxHdr[0] = (u8)((auxHdr[0] & (u8)~0x07U) | 0x05U);

	if (key != NULL) {
		auxHdr[0] |= 0x20U;
		memcpy(auxHdr + 5U, g_zbMacPib.extAddress, sizeof(addrExt_t));
		p = auxHdr + 13U;

		if (key[0] != 5U) {
			if (key[1] == 1U) {
				auxHdr[0] = (u8)((auxHdr[0] & (u8)~0x18U) | 0x10U);
			} else {
				auxHdr[0] |= 0x18U;
			}
		} else {
			auxHdr[0] &= (u8)~0x18U;
		}
	} else if ((extNonceOpt & 0x20U) != 0U) {
		auxHdr[0] |= 0x20U;
		memcpy(auxHdr + 5U, g_zbMacPib.extAddress, sizeof(addrExt_t));
		p = auxHdr + 13U;
		auxHdr[0] &= (u8)~0x18U;
	} else {
		auxHdr[0] &= (u8)~0x18U;
	}

	return (u8)(p - auxHdr);
}

static u8 zb_minimal_next_aps_counter(void)
{
	return g_minimal_aps_counter++;
}

static u8 zb_minimal_next_zdp_seq(void)
{
	if (g_minimal_zdp_seq == 0U) {
		g_minimal_zdp_seq = 1U;
	}

	return g_minimal_zdp_seq++;
}

static zb_minimal_zdp_cb_info_t *zb_minimal_zdp_cb_find(u16 seq)
{
	for (u8 i = 0U; i < ARRAY_SIZE(g_minimal_zdp_cbs); i++) {
		zb_minimal_zdp_cb_info_t *entry = &g_minimal_zdp_cbs[i];

		if (entry->active && entry->seq == seq) {
			return entry;
		}
	}

	return NULL;
}

static void zb_minimal_zdp_cb_store(u16 seq, zdo_callback cb)
{
	zb_minimal_zdp_cb_info_t *entry;

	if (cb == NULL) {
		return;
	}

	entry = &g_minimal_zdp_cbs[g_minimal_zdp_cb_wptr++ & (ZB_MINIMAL_ZDP_CB_MAX - 1U)];
	entry->cb = cb;
	entry->seq = seq;
	entry->used = 1U;
	entry->active = 1U;
}

void tl_zbMinimalZdoResponseIndication(u16 src_addr, u16 cluster_id, const u8 *payload, u8 payload_len)
{
	zb_minimal_zdp_cb_info_t *entry;
	zdo_zdpDataInd_t *ind;
	u8 *zpdu;

	if (payload == NULL || payload_len < 2U || (cluster_id & 0x8000U) == 0U) {
		return;
	}

	entry = zb_minimal_zdp_cb_find(payload[0]);
	if (entry == NULL || entry->cb == NULL) {
		return;
	}

	ind = (zdo_zdpDataInd_t *)ev_buf_allocate(sizeof(*ind) + payload_len);
	if (ind == NULL) {
		entry->active = 0U;
		return;
	}

	zpdu = (u8 *)(ind + 1);
	memcpy(zpdu, payload, payload_len);
	memset(ind, 0, sizeof(*ind));
	ind->zpdu = zpdu;
	ind->src_addr = src_addr;
	ind->clusterId = cluster_id;
	ind->seq_num = payload[0];
	ind->status = payload[1];
	ind->length = payload_len;

	entry->active = 0U;
	entry->cb(ind);
	ev_buf_free((u8 *)ind);
}

static u16 zb_minimal_parent_short_addr_get(void)
{
	u16 parentShortAddr = MAC_SHORT_ADDR_NONE;

	if (tl_zbNwkEdMinimalParentCandidateGet(&parentShortAddr, NULL)) {
		return parentShortAddr;
	}

	return MAC_SHORT_ADDR_NONE;
}

static size_t zb_minimal_build_aps_header(u8 *buf, u8 srcEp, const epInfo_t *dst, u16 clusterId,
					  u8 apsCnt)
{
	u8 fc = 0U;
	size_t idx = 0U;
	bool broadcast = ZB_NWK_IS_ADDRESS_BROADCAST(dst->dstAddr.shortAddr);

	fc |= 0x00U; /* APS data frame */
	fc |= (u8)((broadcast ? 0x02U : 0x00U) << 2); /* delivery mode */
	if ((dst->txOptions & APS_TX_OPT_ACK_TX) != 0U) {
		fc |= BIT(6);
	}

	buf[idx++] = fc;
	buf[idx++] = dst->dstEp;
	COPY_U16TOBUFFER(&buf[idx], clusterId);
	idx += 2U;
	COPY_U16TOBUFFER(&buf[idx], dst->profileId);
	idx += 2U;
	buf[idx++] = srcEp;
	buf[idx++] = apsCnt;

	return idx;
}

static size_t zb_minimal_build_aps_cmd_secure_header(u8 *buf, u8 apsCnt, const u8 *payload)
{
	size_t idx = 0U;

	if ((buf == NULL) || (payload == NULL)) {
		return 0U;
	}

	buf[idx++] = 0x41U;
	buf[idx++] = apsCnt;
	idx += ss_apsEnAuxHdrFill(&buf[idx], (void *)payload, 0);

	return idx;
}

static u8 *zb_minimal_active_nwk_key_get(void)
{
	if (ss_ib.activeSecureMaterialIndex >= SECUR_N_SECUR_MATERIAL) {
		return NULL;
	}

	return ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
}

static size_t zb_minimal_build_nwk_header(u8 *buf, u16 nwkDst, u8 radius, bool security,
					       u32 *frameCounterOut)
{
	u16 fc = 0U;
	size_t idx = 0U;

	fc |= (u16)FRAME_TYPE_DATA;
	fc |= (u16)(0x02U << 2); /* Zigbee PRO nwk protocol version */
	if (security) {
		fc |= BIT(9);
	}
	/*
	 * NWK broadcast uses the destination address alone. Setting the
	 * multicast bit here inserts a multicast control byte and corrupts the
	 * following APS header layout on air.
	 */

	COPY_U16TOBUFFER(&buf[idx], fc);
	idx += 2U;
	COPY_U16TOBUFFER(&buf[idx], nwkDst);
	idx += 2U;
	COPY_U16TOBUFFER(&buf[idx], g_zbNIB.nwkAddr);
	idx += 2U;
	buf[idx++] = (radius != 0U) ? radius : 30U;
	buf[idx++] = g_zbNIB.seqNum++;

	if (security) {
		u32 frameCounter = ss_ib.outgoingFrameCounter++;

		buf[idx++] = ZB_MINIMAL_NWK_SEC_CTRL;
		COPY_U32TOBUFFER(&buf[idx], frameCounter);
		idx += 4U;
		memcpy(&buf[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
		idx += sizeof(addrExt_t);
		buf[idx++] = ss_ib.activeKeySeqNum;

		if (frameCounterOut != NULL) {
			*frameCounterOut = frameCounter;
		}
	} else if (frameCounterOut != NULL) {
		*frameCounterOut = 0U;
	}

	return idx;
}

static size_t zb_minimal_build_mac_header(u8 *buf, u16 macDst)
{
	u16 fcf = 0U;
	size_t idx = 0U;

	fcf |= MAC_FRAME_DATA;
	fcf |= MAC_FCF_ACK_REQ_BIT;
	fcf |= MAC_FCF_INTRA_PAN_MASK;
	fcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_DST_ADDR_MODE_POS;
	fcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_SRC_ADDR_MODE_POS;

	COPY_U16TOBUFFER(&buf[idx], fcf);
	idx += MAC_FCF_FIELD_LEN;
	buf[idx++] = ZB_MAC_DSN();
	ZB_INC_MAC_DSN();
	COPY_U16TOBUFFER(&buf[idx], g_zbNIB.panId);
	idx += MAC_PAN_ID_FIELD_LEN;
	COPY_U16TOBUFFER(&buf[idx], macDst);
	idx += MAC_SHORT_ADDR_FIELD_LEN;
	COPY_U16TOBUFFER(&buf[idx], g_zbNIB.nwkAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;

	return idx;
}

static bool zb_minimal_dev_key_pair_find(const addrExt_t extAddr, ss_dev_pair_set_t *keyPair)
{
	if (extAddr == NULL || keyPair == NULL) {
		return false;
	}

	if (g_ssDevKeyPair.keyPair.used == 0U) {
		return false;
	}
	if (!ZB_64BIT_ADDR_CMP(g_ssDevKeyPair.keyPair.device_address, extAddr)) {
		return false;
	}

	memcpy(keyPair, &g_ssDevKeyPair.keyPair, sizeof(*keyPair));
	return true;
}

static void zb_minimal_dev_key_pair_save(const ss_dev_pair_set_t *keyPair)
{
	if (keyPair == NULL) {
		return;
	}

	memcpy(&g_ssDevKeyPair.keyPair, keyPair, sizeof(g_ssDevKeyPair.keyPair));
	g_ssDevKeyPair.keyPair.used = 1U;
}

static bool zb_minimal_tc_link_key_context_get(ss_dev_pair_set_t *keyPair)
{
	const addrExt_t *tcAddr = NULL;

	if (keyPair == NULL || ss_ib.tcLinkKey == NULL) {
		return false;
	}

	if (!ZB_IEEE_ADDR_IS_ZERO(ss_ib.trust_center_address) &&
	    !ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address)) {
		tcAddr = &ss_ib.trust_center_address;
	} else if (!ZB_IEEE_ADDR_IS_ZERO(g_zbMacPib.coordExtAddress) &&
		   !ZB_IEEE_ADDR_IS_INVALID(g_zbMacPib.coordExtAddress)) {
		tcAddr = &g_zbMacPib.coordExtAddress;
		ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, g_zbMacPib.coordExtAddress);
	}

	if (tcAddr == NULL) {
		return false;
	}

	if (zb_minimal_dev_key_pair_find(*tcAddr, keyPair)) {
		return true;
	}

	memset(keyPair, 0, sizeof(*keyPair));
	ZB_IEEE_ADDR_COPY(keyPair->device_address, *tcAddr);
	memcpy(keyPair->linkKey, ss_ib.tcLinkKey, SEC_KEY_LEN);
	keyPair->apsLinkKeyType = SS_GLOBAL_LINK_KEY;
	keyPair->keyAttr = SS_UNVERIFIED_KEY;
	keyPair->used = 1U;
	zb_minimal_dev_key_pair_save(keyPair);
	return true;
}

static const u8 *zb_minimal_request_key_aps_key_get(u8 secCtrl, const u8 *linkKey,
						      u8 *keyHash)
{
	u8 keyIdentifier;
	u8 pad = 0U;

	if ((linkKey == NULL) || (keyHash == NULL)) {
		return NULL;
	}

	keyIdentifier = (secCtrl >> 3U) & 0x03U;
	switch (keyIdentifier) {
	case SS_SECUR_DATA_KEY:
		return linkKey;
	case SS_SECUR_KEY_TRANSPORT_KEY:
		break;
	case SS_SECUR_KEY_LOAD_KEY:
		pad = 2U;
		break;
	default:
		return NULL;
	}

	if (ss_keyHash(&pad, (u8 *)linkKey, keyHash) != RET_OK) {
		return NULL;
	}

	return keyHash;
}

static int zb_minimal_send_aps_request_key_frame(u16 nwkDst, u16 macDst, const u8 *payload,
						 u8 payload_len)
{
	u8 frame[127];
	u8 aad[15];
	u8 nonce[13];
	u8 apsNonce[13];
	u8 *sec_payload;
	u8 *nwkKey;
	size_t idx = 0U;
	size_t nwkHdrIdx;
	size_t nwkHdrLen;
	size_t nwkPayloadIdx;
	size_t apsHdrIdx;
	size_t apsHdrLen;
	u32 frameCounter;
	u32 nwkFrameCounter = 0U;
	int rc;
	u8 apsCnt;
	u8 attempt;
	u8 apsSecCtrl;
	u8 enc_len;
	u8 keyHash[SEC_KEY_LEN];
	ss_dev_pair_set_t keyPair;
	const u8 *apsKey;
	const u8 *linkKey;

	if ((payload == NULL) || (payload_len == 0U) || (ss_ib.tcLinkKey == NULL)) {
		return -EINVAL;
	}
	if (!zb_minimal_tc_link_key_context_get(&keyPair)) {
		return -EINVAL;
	}
	linkKey = keyPair.linkKey;
	nwkKey = zb_minimal_active_nwk_key_get();
	if (nwkKey == NULL || ss_ib.securityLevel == 0U) {
		return -EACCES;
	}

	apsCnt = zb_minimal_next_aps_counter();

	idx += zb_minimal_build_mac_header(&frame[idx], macDst);
	nwkHdrIdx = idx;
	nwkHdrLen = zb_minimal_build_nwk_header(&frame[idx], nwkDst, 30U, TRUE,
						       &nwkFrameCounter);
	idx += nwkHdrLen;
	nwkPayloadIdx = idx;
	apsHdrIdx = idx;
	apsHdrLen = zb_minimal_build_aps_cmd_secure_header(&frame[idx], apsCnt, payload);
	if ((apsHdrLen == 0U) || (apsHdrLen > sizeof(aad))) {
		return -EINVAL;
	}
	frame[apsHdrIdx] |= 0x20U;
	memcpy(aad, &frame[idx], apsHdrLen);
	apsSecCtrl = frame[apsHdrIdx + 2U];
	frameCounter = BUILD_U32(frame[apsHdrIdx + 3U], frame[apsHdrIdx + 4U],
				 frame[apsHdrIdx + 5U], frame[apsHdrIdx + 6U]);
	apsKey = zb_minimal_request_key_aps_key_get(apsSecCtrl, linkKey, keyHash);
	if (apsKey == NULL) {
		return -EINVAL;
	}
	(void)nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
	idx += apsHdrLen;

	sec_payload = &frame[idx];
	memcpy(sec_payload, payload, payload_len);

	memcpy(apsNonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
	COPY_U32TOBUFFER(&apsNonce[8], frameCounter);
	apsNonce[12] = frame[apsHdrIdx + 2U];
	enc_len = zb_minimal_ccm_encrypt_auth(apsKey, apsNonce, ZB_MINIMAL_APS_MIC_LEN, aad,
					     (u8)apsHdrLen, sec_payload, payload_len,
					     &sec_payload[payload_len]);
	if (enc_len != (u8)(payload_len + ZB_MINIMAL_APS_MIC_LEN)) {
		return -EINVAL;
	}
	idx += enc_len;

	memcpy(nonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
	COPY_U32TOBUFFER(&nonce[8], nwkFrameCounter);
	nonce[12] = ZB_MINIMAL_NWK_SEC_CTRL;
	enc_len = zb_minimal_ccm_encrypt_auth(nwkKey, nonce, ZB_MINIMAL_NWK_MIC_LEN,
					      &frame[nwkHdrIdx], (u8)nwkHdrLen,
					      &frame[nwkPayloadIdx],
					      (u8)(idx - nwkPayloadIdx), &frame[idx]);
	if (enc_len != (u8)((idx - nwkPayloadIdx) + ZB_MINIMAL_NWK_MIC_LEN)) {
		return -EINVAL;
	}
	frame[nwkHdrIdx + 8U] = ZB_MINIMAL_NWK_SEC_CTRL_WIRE;
	idx = nwkPayloadIdx + enc_len;

	for (attempt = 0U; attempt < ZB_MINIMAL_ZDO_TX_RETRIES; attempt++) {
		rc = zb_platform_radio_send_raw_psdu(frame, (u8)idx);
		if (rc >= 0) {
			LOG_INF("minimal APS RequestKey tx dst=0x%04x mac=0x%04x aps=%u fc=%u",
				nwkDst, macDst, apsCnt, frameCounter);
			return RET_OK;
		}

		if (rc != -EBUSY && rc != -EAGAIN) {
			break;
		}

		k_busy_wait(ZB_MINIMAL_ZDO_TX_RETRY_DELAY_US);
	}

	LOG_WRN("minimal APS RequestKey tx failed rc=%d", rc);
	return RET_OPERATION_FAILED;
}

static void zb_minimal_request_key_task(void *arg)
{
	ss_apsmeRequestKeyReq_t *req = (ss_apsmeRequestKeyReq_t *)arg;
	u8 payload[2U + sizeof(addrExt_t)];
	u8 payload_len = 2U;
	u16 nwkDst;
	u16 macDst;
	int sendStatus;

	zb_request_key_trace[1] = 0U;
	zb_request_key_trace[2] = 0U;
	zb_request_key_trace[3] = 0xa9b0ff00U;

	if (req == NULL) {
		zb_request_key_trace[3] = 0xa9b0ff01U;
		return;
	}

	if ((ss_ib.tcLinkKey == NULL) ||
	    (req->dstAddrMode != ZB_ADDR_16BIT_DEV_OR_BROADCAST) ||
	    (req->keyType != SS_KEYREQ_TYPE_TCLK)) {
		zb_request_key_trace[3] = 0xa9b0ff02U;
		ev_buf_free((u8 *)req);
		return;
	}

	nwkDst = req->dstAddr.shortAddr;
	macDst = zb_minimal_parent_short_addr_get();
	if (macDst == MAC_SHORT_ADDR_NONE) {
		macDst = nwkDst;
	}
	zb_request_key_trace[1] = ((u32)nwkDst << 16) | macDst;
	zb_request_key_trace[2] = ((u32)req->keyType << 24) |
				  ((u32)req->dstAddrMode << 16) | req->dstAddr.shortAddr;

	payload[0] = 0x08U;
	payload[1] = req->keyType;

	if (req->keyType == SS_KEYREQ_TYPE_APPLK) {
		memcpy(&payload[2], req->partnerAddr, sizeof(addrExt_t));
		payload_len = sizeof(payload);
	}

	sendStatus = zb_minimal_send_aps_request_key_frame(nwkDst, macDst, payload, payload_len);
	zb_request_key_trace[3] = 0xa9b10000U | (u16)sendStatus;
	ev_buf_free((u8 *)req);
}

static u8 zb_minimal_send_aps_data_frame(u8 srcEp, const epInfo_t *dst, u16 clusterId,
					 u16 cmdPldLen, const u8 *cmdPld, u8 *apsCnt)
{
	u8 frame[127];
	u8 nonce[13];
	u8 *nwkKey = NULL;
	size_t idx = 0U;
	size_t nwkHdrIdx;
	size_t apsHdrIdx;
	size_t apsHdrLen;
	size_t nwkHdrLen;
	u16 macDst;
	u8 localApsCnt;
	u32 nwkFrameCounter = 0U;
	int rc;
	u8 attempt;
	bool useNwkSecurity;

	if ((dst == NULL) || (cmdPld == NULL)) {
		return APS_STATUS_INVALID_PARAMETER;
	}

	if (!zb_isDeviceJoinedNwk() && !tl_zbNwkEdMinimalCanProcessDataFrames()) {
		return APS_STATUS_ILLEGAL_REQUEST;
	}

	if (dst->dstAddrMode != APS_SHORT_DSTADDR_WITHEP) {
		return APS_STATUS_NOT_SUPPORTED;
	}

	macDst = zb_minimal_parent_short_addr_get();
	if (macDst == MAC_SHORT_ADDR_NONE) {
		if (ZB_NWK_IS_ADDRESS_BROADCAST(dst->dstAddr.shortAddr)) {
			macDst = NWK_BROADCAST_ALL_DEVICES;
		} else {
			macDst = dst->dstAddr.shortAddr;
		}
	}

	useNwkSecurity = !ZB_NWK_IS_ADDRESS_BROADCAST(dst->dstAddr.shortAddr) &&
			 (ss_ib.securityLevel != 0U);
	if (useNwkSecurity) {
		nwkKey = zb_minimal_active_nwk_key_get();
		if (nwkKey == NULL) {
			return APS_STATUS_SECURITY_FAIL;
		}
	}

	localApsCnt = zb_minimal_next_aps_counter();
	idx += zb_minimal_build_mac_header(&frame[idx], macDst);
	nwkHdrIdx = idx;
	nwkHdrLen = zb_minimal_build_nwk_header(&frame[idx], dst->dstAddr.shortAddr, dst->radius,
						      useNwkSecurity, &nwkFrameCounter);
	idx += nwkHdrLen;
	apsHdrIdx = idx;
	apsHdrLen = zb_minimal_build_aps_header(&frame[idx], srcEp, dst, clusterId, localApsCnt);
	idx += apsHdrLen;

	if (idx + cmdPldLen > sizeof(frame)) {
		return APS_STATUS_ASDU_TOO_LONG;
	}

	memcpy(&frame[idx], cmdPld, cmdPldLen);
	idx += cmdPldLen;

	if (useNwkSecurity) {
		u8 encLen;

		memcpy(nonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
		COPY_U32TOBUFFER(&nonce[8], nwkFrameCounter);
		nonce[12] = ZB_MINIMAL_NWK_SEC_CTRL;
		encLen = zb_minimal_ccm_encrypt_auth(nwkKey, nonce, ZB_MINIMAL_NWK_MIC_LEN,
						     &frame[nwkHdrIdx], (u8)nwkHdrLen,
						     &frame[apsHdrIdx],
						     (u8)(apsHdrLen + cmdPldLen),
						     &frame[idx]);
		if (encLen != (u8)(apsHdrLen + cmdPldLen + ZB_MINIMAL_NWK_MIC_LEN)) {
			return APS_STATUS_SECURITY_FAIL;
		}

		frame[nwkHdrIdx + 8U] = ZB_MINIMAL_NWK_SEC_CTRL_WIRE;
		idx = apsHdrIdx + encLen;
		(void)nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);
	}

	rc = -EBUSY;
	for (attempt = 0U; attempt < ZB_MINIMAL_ZDO_TX_RETRIES; attempt++) {
		rc = zb_platform_radio_send_raw_psdu(frame, (u8)idx);
		if (rc >= 0) {
			break;
		}

		if (rc != -EBUSY && rc != -EAGAIN) {
			break;
		}

		k_busy_wait(ZB_MINIMAL_ZDO_TX_RETRY_DELAY_US);
	}

	if (rc < 0) {
		LOG_WRN("minimal ZDO tx failed rc=%d dst=0x%04x cluster=0x%04x len=%u attempt=%u",
			rc, dst->dstAddr.shortAddr, clusterId, cmdPldLen, attempt + 1U);
		return APS_STATUS_INTERNAL_BUF_FULL;
	}

	if (apsCnt != NULL) {
		*apsCnt = localApsCnt;
	}

	LOG_INF("minimal ZDO tx dst=0x%04x mac=0x%04x cluster=0x%04x aps=%u nwk=%u len=%u",
		dst->dstAddr.shortAddr, macDst, clusterId, localApsCnt,
		(u8)(g_zbNIB.seqNum - 1U), cmdPldLen);

	return APS_STATUS_SUCCESS;
}

u8 af_dataSend(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 cmdPldLen, u8 *cmdPld, u8 *apsCnt)
{
	if (srcEp > 240U) {
		return APS_STATUS_NOT_SUPPORTED;
	}

	return zb_minimal_send_aps_data_frame(srcEp, pDstEpInfo, clusterId, cmdPldLen, cmdPld,
					      apsCnt);
}

u8 zdo_send_req(zdo_zdp_req_t *req)
{
	epInfo_t dstEpInfo;
	u8 apsCnt = 0U;
	u8 status;

	if ((req == NULL) || (req->zdu == NULL)) {
		return APS_STATUS_INVALID_PARAMETER;
	}

	TL_SETSTRUCTCONTENT(dstEpInfo, 0);
	dstEpInfo.profileId = ZDO_PROFILE_ID;
	dstEpInfo.dstEp = ZDO_EP;
	dstEpInfo.radius = 30U;
	dstEpInfo.txOptions = APS_TX_OPT_ACK_TX;

	if (req->dst_addr_mode != SHORT_ADDR_MODE) {
		LOG_WRN("minimal zdo_send_req only supports short dst address mode");
		return APS_STATUS_NOT_SUPPORTED;
	}

	dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dstEpInfo.dstAddr.shortAddr = req->dst_nwk_addr;
	status = af_dataSend(ZDO_EP, &dstEpInfo, req->cluster_id, req->zduLen, req->zdu, &apsCnt);
	if (status == APS_STATUS_SUCCESS) {
		req->zdpSeqNum = req->zdu[0];
		zb_minimal_zdp_cb_store(req->zdpSeqNum, req->zdoRspReceivedIndCb);
	}

	return status;
}

zdo_status_t zb_mgmtPermitJoinReq(u16 dstNwkAddr, u8 permitJoinDuration, u8 tcSignificance, u8 *seqNo,
				  zdo_callback indCb)
{
	u8 payload[3];
	zdo_zdp_req_t req;

	ARG_UNUSED(indCb);

	payload[0] = zb_minimal_next_zdp_seq();
	payload[1] = permitJoinDuration;
	payload[2] = (tcSignificance != 0U) ? 1U : 0U;

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = sizeof(payload);
	req.cluster_id = MGMT_PERMIT_JOINING_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	return zdo_send_req(&req);
}

zdo_status_t zb_zdoNwkAddrReq(u16 dstNwkAddr, zdo_nwk_addr_req_t *pReq, u8 *seqNo, zdo_callback indCb)
{
	u8 payload[1U + sizeof(addrExt_t) + 2U];
	zdo_zdp_req_t req;

	if (pReq == NULL) {
		return ZDO_INVALID_REQUEST;
	}

	payload[0] = zb_minimal_next_zdp_seq();
	memcpy(&payload[1], pReq->ieee_addr_interest, sizeof(addrExt_t));
	payload[9] = pReq->req_type;
	payload[10] = pReq->start_index;

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = sizeof(payload);
	req.cluster_id = NWK_ADDR_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.zdoRspReceivedIndCb = indCb;
	return zdo_send_req(&req);
}

zdo_status_t zb_zdoIeeeAddrReq(u16 dstNwkAddr, zdo_ieee_addr_req_t *pReq, u8 *seqNo, zdo_callback indCb)
{
	u8 payload[5];
	zdo_zdp_req_t req;

	if (pReq == NULL) {
		return ZDO_INVALID_REQUEST;
	}

	payload[0] = zb_minimal_next_zdp_seq();
	COPY_U16TOBUFFER(&payload[1], pReq->nwk_addr_interest);
	payload[3] = pReq->req_type;
	payload[4] = pReq->start_index;

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = sizeof(payload);
	req.cluster_id = IEEE_ADDR_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.zdoRspReceivedIndCb = indCb;
	return zdo_send_req(&req);
}

zdo_status_t zb_zdoSimpleDescReq(u16 dstNwkAddr, zdo_simple_descriptor_req_t *pReq, u8 *seqNo,
				       zdo_callback indCb)
{
	u8 payload[4];
	zdo_zdp_req_t req;

	if (pReq == NULL) {
		return ZDO_INVALID_REQUEST;
	}

	payload[0] = zb_minimal_next_zdp_seq();
	COPY_U16TOBUFFER(&payload[1], pReq->nwk_addr_interest);
	payload[3] = pReq->endpoint;

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = sizeof(payload);
	req.cluster_id = SIMPLE_DESC_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.zdoRspReceivedIndCb = indCb;
	return zdo_send_req(&req);
}

zdo_status_t zb_zdoNodeDescReq(u16 dstNwkAddr, zdo_node_descriptor_req_t *pReq, u8 *seqNo,
				     zdo_callback indCb)
{
	u8 payload[3];
	zdo_zdp_req_t req;

	if (pReq == NULL) {
		return ZDO_INVALID_REQUEST;
	}

	payload[0] = zb_minimal_next_zdp_seq();
	COPY_U16TOBUFFER(&payload[1], pReq->nwk_addr_interest);

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = sizeof(payload);
	req.cluster_id = NODE_DESC_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.zdoRspReceivedIndCb = indCb;
	return zdo_send_req(&req);
}

zdo_status_t zb_zdoPowerDescReq(u16 dstNwkAddr, zdo_power_descriptor_req_t *pReq, u8 *seqNo,
				      zdo_callback indCb)
{
	u8 payload[3];
	zdo_zdp_req_t req;

	if (pReq == NULL) {
		return ZDO_INVALID_REQUEST;
	}

	payload[0] = zb_minimal_next_zdp_seq();
	COPY_U16TOBUFFER(&payload[1], pReq->nwk_addr_interest);

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = sizeof(payload);
	req.cluster_id = POWER_DESC_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.zdoRspReceivedIndCb = indCb;
	return zdo_send_req(&req);
}

zdo_status_t zb_zdoActiveEpReq(u16 dstNwkAddr, zdo_active_ep_req_t *pReq, u8 *seqNo,
				     zdo_callback indCb)
{
	u8 payload[3];
	zdo_zdp_req_t req;

	if (pReq == NULL) {
		return ZDO_INVALID_REQUEST;
	}

	payload[0] = zb_minimal_next_zdp_seq();
	COPY_U16TOBUFFER(&payload[1], pReq->nwk_addr_interest);

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = sizeof(payload);
	req.cluster_id = ACTIVE_EP_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.zdoRspReceivedIndCb = indCb;
	return zdo_send_req(&req);
}

zdo_status_t zb_zdoMatchDescReq(u16 dstNwkAddr, zdo_match_descriptor_req_t *pReq, u8 *seqNo,
				      zdo_callback indCb)
{
	u8 payload[6U + (u8)((pReq != NULL) ? ((pReq->num_in_clusters + pReq->num_out_clusters) * 2U) : 0U)];
	u8 *ptr = payload;
	zdo_zdp_req_t req;
	u8 total_clusters;

	if (pReq == NULL) {
		return ZDO_INVALID_REQUEST;
	}

	total_clusters = (u8)(pReq->num_in_clusters + pReq->num_out_clusters);
	if (total_clusters > (u8)(2U * MAX_REQUESTED_CLUSTER_NUMBER)) {
		return ZDO_INVALID_REQUEST;
	}

	*ptr++ = zb_minimal_next_zdp_seq();
	COPY_U16TOBUFFER(ptr, pReq->nwk_addr_interest);
	ptr += 2U;
	COPY_U16TOBUFFER(ptr, pReq->profile_id);
	ptr += 2U;
	*ptr++ = pReq->num_in_clusters;
	for (u8 i = 0U; i < pReq->num_in_clusters; i++) {
		COPY_U16TOBUFFER(ptr, pReq->cluster_list[i]);
		ptr += 2U;
	}
	*ptr++ = pReq->num_out_clusters;
	for (u8 i = 0U; i < pReq->num_out_clusters; i++) {
		COPY_U16TOBUFFER(ptr, pReq->cluster_list[pReq->num_in_clusters + i]);
		ptr += 2U;
	}

	if (seqNo != NULL) {
		*seqNo = payload[0];
	}

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = (u8)(ptr - payload);
	req.cluster_id = MATCH_DESC_REQ_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = dstNwkAddr;
	req.zdoRspReceivedIndCb = indCb;
	return zdo_send_req(&req);
}

u8 zb_zdoSendDevAnnance(void)
{
	u8 payload[1U + sizeof(zdo_device_annce_req_t)];
	size_t idx = 0U;
	zdo_zdp_req_t req;
	u8 capability = 0U;
	u8 status;

	/* Let the association response exchange clear before the first broadcast
	 * ZDO frame. Without this gap the coordinator side often misses the
	 * immediate Device_annce even when local TX reports success. */
	k_sleep(K_MSEC(40));

	payload[idx++] = zb_minimal_next_zdp_seq();
	COPY_U16TOBUFFER(&payload[idx], g_zbNIB.nwkAddr);
	idx += 2U;
	memcpy(&payload[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
	idx += sizeof(addrExt_t);

	capability |= g_zbNIB.capabilityInfo.altPanCoord ? BIT(0) : 0U;
	capability |= g_zbNIB.capabilityInfo.devType ? BIT(1) : 0U;
	capability |= g_zbNIB.capabilityInfo.powerSrc ? BIT(2) : 0U;
	capability |= g_zbMacPib.rxOnWhenIdle ? BIT(3) : 0U;
	capability |= g_zbNIB.capabilityInfo.secuCapability ? BIT(6) : 0U;
	capability |= BIT(7);
	payload[idx++] = capability;

	TL_SETSTRUCTCONTENT(req, 0);
	req.zdu = payload;
	req.zduLen = (u8)idx;
	req.cluster_id = DEVICE_ANNCE_CLID;
	req.dst_addr_mode = SHORT_ADDR_MODE;
	req.dst_nwk_addr = NWK_BROADCAST_RX_ON_WHEN_IDLE;

	status = zdo_send_req(&req);
	return status;
}

u8 zb_apsmeRequestKeyReq(ss_apsmeRequestKeyReq_t *pRequestKeyReq)
{
	ss_apsmeRequestKeyReq_t *reqCopy;

	if ((pRequestKeyReq == NULL) || (ss_ib.tcLinkKey == NULL)) {
		return RET_INVALID_PARAMETER;
	}

	if ((pRequestKeyReq->dstAddrMode != ZB_ADDR_16BIT_DEV_OR_BROADCAST) ||
	    (pRequestKeyReq->keyType != SS_KEYREQ_TYPE_TCLK)) {
		return RET_OPERATION_FAILED;
	}

	reqCopy = (ss_apsmeRequestKeyReq_t *)ev_buf_allocate(sizeof(*reqCopy));
	if (reqCopy == NULL) {
		return RET_NO_MEMORY;
	}

	memcpy(reqCopy, pRequestKeyReq, sizeof(*reqCopy));

	if (TL_SCHEDULE_TASK(zb_minimal_request_key_task, reqCopy) != RET_OK) {
		ev_buf_free((u8 *)reqCopy);
		return RET_BUSY;
	}

	return RET_OK;
}

u16 zb_getLocalShortAddr(void)
{
	return g_zbNIB.nwkAddr;
}

void zb_getLocalExtAddr(addrExt_t extAddr)
{
	if (extAddr != NULL) {
		memcpy(extAddr, g_zbMacPib.extAddress, sizeof(addrExt_t));
	}
}

u16 zb_getParentShortAddr(void)
{
	return zb_minimal_parent_short_addr_get();
}

device_type_t zb_getDeviceType(void)
{
	return DEVICE_TYPE_END_DEVICE;
}
