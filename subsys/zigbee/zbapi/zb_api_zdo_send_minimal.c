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

LOG_MODULE_REGISTER(zigbee_zdo_tx_minimal, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZB_MINIMAL_ZDO_TX_RETRIES          4U
#define ZB_MINIMAL_ZDO_TX_RETRY_DELAY_US   5000U
#define ZB_MINIMAL_APS_MIC_LEN             4U
#define ZB_MINIMAL_APS_CCM_L_VAL           2U
#define ZB_MINIMAL_APS_SEC_CTRL            0x25U
#define ZB_MINIMAL_APS_FC_BOOTSTRAP        0x01000000U

extern bool tl_zbNwkEdMinimalParentCandidateGet(u16 *parentShortAddr, addrExt_t parentIeeeAddr);
extern u8 ss_ccmEncryption(u8 *key, u8 *nonce, u8 aStrLen, u8 *aStr, u8 mStrLen, u8 *mStr);

static u8 g_minimal_aps_counter;
static u8 g_minimal_zdp_seq;

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

static u16 zb_minimal_parent_short_addr_get(void)
{
	u16 parentShortAddr = MAC_SHORT_ADDR_NONE;

	if (tl_zbNwkEdMinimalParentCandidateGet(&parentShortAddr, NULL)) {
		return parentShortAddr;
	}

	return MAC_SHORT_ADDR_NONE;
}

static size_t zb_minimal_build_aps_header(u8 *buf, const epInfo_t *dst, u16 clusterId, u8 apsCnt)
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
	buf[idx++] = ZDO_EP;
	buf[idx++] = apsCnt;

	return idx;
}

static size_t zb_minimal_build_aps_cmd_secure_header(u8 *buf, u8 apsCnt, u32 frameCounter)
{
	size_t idx = 0U;

	buf[idx++] = 0x21U;
	buf[idx++] = apsCnt;
	buf[idx++] = ZB_MINIMAL_APS_SEC_CTRL;
	COPY_U32TOBUFFER(&buf[idx], frameCounter);
	idx += 4U;
	memcpy(&buf[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
	idx += sizeof(addrExt_t);

	return idx;
}

static size_t zb_minimal_build_nwk_header(u8 *buf, u16 nwkDst, u8 radius)
{
	u16 fc = 0U;
	size_t idx = 0U;

	fc |= (u16)FRAME_TYPE_DATA;
	fc |= (u16)(0x02U << 2); /* Zigbee PRO nwk protocol version */
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

static int zb_minimal_send_aps_request_key_frame(u16 nwkDst, u16 macDst, const u8 *payload,
						 u8 payload_len)
{
	u8 frame[127];
	u8 aad[15];
	u8 nonce[13];
	u8 *sec_payload;
	size_t idx = 0U;
	size_t apsHdrLen;
	u32 frameCounter;
	int rc;
	u8 apsCnt;
	u8 attempt;
	u8 enc_len;
	ss_dev_pair_set_t keyPair;
	const u8 *linkKey;

	if ((payload == NULL) || (payload_len == 0U) || (ss_ib.tcLinkKey == NULL)) {
		return -EINVAL;
	}
	if (!zb_minimal_tc_link_key_context_get(&keyPair)) {
		return -EINVAL;
	}
	linkKey = keyPair.linkKey;

	apsCnt = zb_minimal_next_aps_counter();
	if (keyPair.outgoingFrameCounter < ZB_MINIMAL_APS_FC_BOOTSTRAP) {
		keyPair.outgoingFrameCounter = ZB_MINIMAL_APS_FC_BOOTSTRAP;
	}
	frameCounter = ++keyPair.outgoingFrameCounter;
	zb_minimal_dev_key_pair_save(&keyPair);
	(void)nv_nwkFrameCountSaveToFlash(frameCounter);

	idx += zb_minimal_build_mac_header(&frame[idx], macDst);
	idx += zb_minimal_build_nwk_header(&frame[idx], nwkDst, 30U);
	apsHdrLen = zb_minimal_build_aps_cmd_secure_header(&frame[idx], apsCnt, frameCounter);
	memcpy(aad, &frame[idx], apsHdrLen);
	idx += apsHdrLen;

	sec_payload = &frame[idx];
	memcpy(sec_payload, payload, payload_len);

	memcpy(nonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
	COPY_U32TOBUFFER(&nonce[8], frameCounter);
	nonce[12] = ZB_MINIMAL_APS_SEC_CTRL;
	enc_len = ss_ccmEncryption((u8 *)linkKey, nonce, sizeof(aad), aad, payload_len,
				 sec_payload);
	if (enc_len != (u8)(payload_len + ZB_MINIMAL_APS_MIC_LEN)) {
		return -EINVAL;
	}
	idx += enc_len;

	for (attempt = 0U; attempt < ZB_MINIMAL_ZDO_TX_RETRIES; attempt++) {
		rc = zb_platform_radio_send_raw_psdu(frame, (u8)idx);
		if (rc >= 0) {
			LOG_INF("minimal APS RequestKey tx dst=0x%04x mac=0x%04x aps=%u fc=%u",
				nwkDst, macDst, apsCnt, frameCounter);
			return RET_OK;
		}

		if (rc != -EBUSY) {
			break;
		}

		k_busy_wait(ZB_MINIMAL_ZDO_TX_RETRY_DELAY_US);
	}

	LOG_WRN("minimal APS RequestKey tx failed rc=%d", rc);
	return RET_OPERATION_FAILED;
}

static u8 zb_minimal_send_zdo_frame(const epInfo_t *dst, u16 clusterId, u16 cmdPldLen, const u8 *cmdPld,
				    u8 *apsCnt)
{
	u8 frame[127];
	size_t idx = 0U;
	size_t apsHdrLen;
	size_t nwkHdrLen;
	u16 macDst;
	u8 localApsCnt;
	int rc;
	u8 attempt;

	if ((dst == NULL) || (cmdPld == NULL)) {
		return APS_STATUS_INVALID_PARAMETER;
	}

	if (!zb_isDeviceJoinedNwk()) {
		return APS_STATUS_ILLEGAL_REQUEST;
	}

	if ((dst->profileId != ZDO_PROFILE_ID) || (dst->dstAddrMode != APS_SHORT_DSTADDR_WITHEP)) {
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

	localApsCnt = zb_minimal_next_aps_counter();
	idx += zb_minimal_build_mac_header(&frame[idx], macDst);
	nwkHdrLen = zb_minimal_build_nwk_header(&frame[idx], dst->dstAddr.shortAddr, dst->radius);
	idx += nwkHdrLen;
	apsHdrLen = zb_minimal_build_aps_header(&frame[idx], dst, clusterId, localApsCnt);
	idx += apsHdrLen;

	if (idx + cmdPldLen > sizeof(frame)) {
		return APS_STATUS_ASDU_TOO_LONG;
	}

	memcpy(&frame[idx], cmdPld, cmdPldLen);
	idx += cmdPldLen;

	rc = -EBUSY;
	for (attempt = 0U; attempt < ZB_MINIMAL_ZDO_TX_RETRIES; attempt++) {
		rc = zb_platform_radio_send_raw_psdu(frame, (u8)idx);
		if (rc >= 0) {
			break;
		}

		if (rc != -EBUSY) {
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
	if (srcEp != ZDO_EP) {
		return APS_STATUS_NOT_SUPPORTED;
	}

	return zb_minimal_send_zdo_frame(pDstEpInfo, clusterId, cmdPldLen, cmdPld, apsCnt);
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
	dstEpInfo.txOptions = 0U;

	if (req->dst_addr_mode != SHORT_ADDR_MODE) {
		LOG_WRN("minimal zdo_send_req only supports short dst address mode");
		return APS_STATUS_NOT_SUPPORTED;
	}

	dstEpInfo.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dstEpInfo.dstAddr.shortAddr = req->dst_nwk_addr;
	status = af_dataSend(ZDO_EP, &dstEpInfo, req->cluster_id, req->zduLen, req->zdu, &apsCnt);
	if (status == APS_STATUS_SUCCESS) {
		req->zdpSeqNum = req->zdu[0];
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
	u8 payload[2U + sizeof(addrExt_t)];
	u8 payload_len = 2U;
	u16 nwkDst;
	u16 macDst;

	if ((pRequestKeyReq == NULL) || (ss_ib.tcLinkKey == NULL)) {
		return RET_INVALID_PARAMETER;
	}

	if ((pRequestKeyReq->dstAddrMode != ZB_ADDR_16BIT_DEV_OR_BROADCAST) ||
	    (pRequestKeyReq->keyType != SS_KEYREQ_TYPE_TCLK)) {
		return RET_OPERATION_FAILED;
	}

	nwkDst = pRequestKeyReq->dstAddr.shortAddr;
	macDst = zb_minimal_parent_short_addr_get();
	if (macDst == MAC_SHORT_ADDR_NONE) {
		macDst = nwkDst;
	}

	payload[0] = 0x08U;
	payload[1] = pRequestKeyReq->keyType;

	if (pRequestKeyReq->keyType == SS_KEYREQ_TYPE_APPLK) {
		memcpy(&payload[2], pRequestKeyReq->partnerAddr, sizeof(addrExt_t));
		payload_len = sizeof(payload);
	}

	return (u8)zb_minimal_send_aps_request_key_frame(nwkDst, macDst, payload, payload_len);
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
