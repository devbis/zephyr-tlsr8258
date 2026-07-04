/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "tl_platform.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include "zb_common_stub.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "mac/includes/mac_trx_api.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/mac_phy.h"
#include "zb_minimal_ccm.h"

LOG_MODULE_REGISTER(zigbee_mac_trx_compat, CONFIG_ZIGBEE_LOG_LEVEL);

/* g_sysDiags now lives in subsys/zigbee/common/zb_config.c
 * (copied from tl_zigbee_sdk/zigbee/common/zb_config.c).
 */
zb_info_t g_zbInfo;

extern const u8 tcLinkKeyCentralDefault[];
extern u8 ss_keyHash(u8 *input, u8 *key, u8 *output);
extern void tl_zbNwkEdMinimalRuntimeReset(void);
extern void zdo_ed_minimal_rejoin_restart_prepare(void);
extern void app_bdb_rejoin_callback_trace_put(uint32_t tag);

__attribute__((weak)) void tl_zbNwkEdMinimalMacRxIndicate(const u8 *macPld, u8 len, s8 rssi)
{
	ARG_UNUSED(macPld);
	ARG_UNUSED(len);
	ARG_UNUSED(rssi);
}

__attribute__((weak)) bool tl_zbNwkEdMinimalCanProcessDataFrames(void)
{
	return g_zbNwkCtx.joined ? TRUE : FALSE;
}

__attribute__((weak)) void tl_zbNwkEdMinimalTransportKeyDone(void)
{
}

__attribute__((weak)) void tl_zbNwkEdMinimalTimeoutRspReceived(u8 status, u8 parentInfo)
{
	ARG_UNUSED(status);
	ARG_UNUSED(parentInfo);
}

__attribute__((weak)) void tl_zbNwkEdMinimalRejoinResponseReceived(u8 status, u16 nwkAddr,
							       u16 parentShortAddr)
{
	ARG_UNUSED(status);
	ARG_UNUSED(nwkAddr);
	ARG_UNUSED(parentShortAddr);
}

__attribute__((weak)) void tl_zbNwkEdMinimalInterviewPollStart(u8 count, u32 intervalMs)
{
	ARG_UNUSED(count);
	ARG_UNUSED(intervalMs);
}

__attribute__((weak)) void tl_zbMinimalZdoResponseIndication(u16 src_addr, u16 cluster_id,
							     const u8 *payload, u8 payload_len)
{
	ARG_UNUSED(src_addr);
	ARG_UNUSED(cluster_id);
	ARG_UNUSED(payload);
	ARG_UNUSED(payload_len);
}

__attribute__((weak)) u8 sys_exceptionPost(u16 line, u8 evt)
{
	ARG_UNUSED(line);
	ARG_UNUSED(evt);
	return 0U;
}

static u16 zb_u16_from_le(const u8 *src)
{
	return (u16)src[0] | ((u16)src[1] << 8);
}

static u32 zb_u32_from_le(const u8 *src)
{
	return (u32)src[0] | ((u32)src[1] << 8) | ((u32)src[2] << 16) | ((u32)src[3] << 24);
}

/*
 * Resolve the APS security source IEEE address when the 802.15.4 frame header
 * carries only a short address (keyIdMode == SS_KEY_ID_MODE_IMPLICIT).
 * Prefers trust_center_address (populated on first Transport-Key delivery)
 * over coordExtAddress; returns NULL to drop the frame rather than decrypt
 * with a zeroed IEEE address.
 *
 * This path (first secured unicast from TC after join when coordExtAddress is
 * not yet populated) cannot be exercised by host-only baselines.  Full
 * validation is deferred to on-hardware testing in a later task.
 */
static const u8 *zb_minimal_aps_security_src_fallback_get(void)
{
	if (!ZB_IEEE_ADDR_IS_ZERO(ss_ib.trust_center_address) &&
	    !ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address)) {
		return ss_ib.trust_center_address;
	}

	if (!ZB_IEEE_ADDR_IS_ZERO(g_zbMacPib.coordExtAddress) &&
	    !ZB_IEEE_ADDR_IS_INVALID(g_zbMacPib.coordExtAddress)) {
		return g_zbMacPib.coordExtAddress;
	}

	return NULL;
}

static u8 zb_mac_src_addr_ptr(u16 frame_ctrl, u8 len, u8 *has_src_addr, u8 *malformed)
{
	u8 idx = MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN;
	u8 dst_mode = (u8)((frame_ctrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	u8 src_mode = (u8)((frame_ctrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);
	u8 src_len;

	*has_src_addr = 0;
	*malformed = 0;

	if (dst_mode != ZB_ADDR_NO_ADDR) {
		idx += MAC_PAN_ID_FIELD_LEN;
		idx += (dst_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	}

	if (src_mode == ZB_ADDR_NO_ADDR) {
		return 0;
	}

	if ((frame_ctrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
		idx += MAC_PAN_ID_FIELD_LEN;
	}

	src_len = (src_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	if ((u16)idx + src_len > len) {
		*malformed = 1;
		return 0;
	}

	*has_src_addr = 1;
	return idx;
}

u8 *zb_macDataFilter(u8 *macPld, u8 len, u8 *needDrop, u8 *ackPkt)
{
	u16 frame_ctrl;
	u8 frame_type;
	u8 has_src_addr;
	u8 malformed;
	u8 src_idx;

	if (needDrop != NULL) {
		*needDrop = 0;
	}
	if (ackPkt != NULL) {
		*ackPkt = 0;
	}

	if (macPld == NULL || len < MAC_MIN_HDR_LEN) {
		if (needDrop != NULL) {
			*needDrop = 1;
		}
		return macPld;
	}

	frame_ctrl = (u16)macPld[0] | ((u16)macPld[1] << 8);
	frame_type = (u8)((frame_ctrl & MAC_FCF_FRAME_TYPE_MASK) >> MAC_FCF_FRAME_TYPE_POS);
	if (ackPkt != NULL && frame_type == MAC_FRAME_TYPE_ACK) {
		*ackPkt = 1;
	}

	src_idx = zb_mac_src_addr_ptr(frame_ctrl, len, &has_src_addr, &malformed);
	if (malformed) {
		if (needDrop != NULL) {
			*needDrop = 1;
		}
		return macPld;
	}

	if (!has_src_addr) {
		return macPld;
	}

	return &macPld[src_idx];
}

typedef struct {
	u16 frame_ctrl;
	u8 header_len;
	u8 payload_len;
	const u8 *payload;
} zb_minimal_mac_frame_t;

typedef struct {
	u16 frame_ctrl;
	bool dst_short_valid;
	u16 dst_short_addr;
	bool dst_ext_valid;
	addrExt_t dst_ext_addr;
	bool src_short_valid;
	u16 src_short_addr;
	bool src_ext_valid;
	addrExt_t src_ext_addr;
} zb_minimal_mac_addr_info_t;

typedef struct {
	u16 frame_ctrl;
	u16 dst_addr;
	u16 src_addr;
	u8 header_len;
	u8 payload_len;
	const u8 *payload;
	bool security;
	bool multicast;
	bool src_ieee;
	bool dst_ieee;
	bool source_route;
	u8 frame_type;
	u8 security_control;
	u8 key_seq;
	u8 mic_len;
	u32 frame_counter;
	addrExt_t security_src_ext;
} zb_minimal_nwk_frame_t;

typedef struct {
	u8 frame_control;
	u8 dst_ep;
	u16 cluster_id;
	u16 profile_id;
	u8 src_ep;
	u8 counter;
	u8 header_len;
	u8 payload_len;
	const u8 *payload;
	bool security;
	bool ack_req;
	bool extended_hdr;
	u8 frame_type;
	u8 delivery_mode;
	u8 aps_counter;
	u8 security_control;
	u8 key_id;
	u8 mic_len;
	u32 frame_counter;
	addrExt_t security_src_ext;
} zb_minimal_aps_frame_t;

typedef struct {
	bool pending;
	u16 dst_nwk_addr;
	u16 cluster_id;
	u16 payload_len;
	u8 payload[64];
} zb_minimal_pending_zdo_rsp_t;

typedef struct {
	bool pending;
	u16 dst_nwk_addr;
	u16 profile_id;
	u16 cluster_id;
	u16 payload_len;
	u8 src_ep;
	u8 dst_ep;
	u8 payload[64];
} zb_minimal_pending_zcl_rsp_t;

typedef struct {
	bool pending;
	u16 requester_nwk_addr;
	u8 zdo_seq;
	u8 leave_options;
	bool send_leave_command;
	bool send_zdo_response;
} zb_minimal_pending_leave_t;

#define ZB_MINIMAL_NWK_AUX_HDR_LEN 14U
#define ZB_MINIMAL_NWK_MIC_LEN     4U
#define ZB_MINIMAL_APS_MIC_LEN     4U
#define ZB_MINIMAL_TX_RETRIES      4U
#define ZB_MINIMAL_TX_RETRY_US     5000U
#define ZB_MINIMAL_NWK_SEC_CTRL    0x2DU
#define ZB_MINIMAL_NWK_SEC_CTRL_WIRE 0x28U
#define ZB_MINIMAL_MGMT_LEAVE_REMOVE_CHILDREN BIT(6)
#define ZB_MINIMAL_MGMT_LEAVE_REJOIN          BIT(7)
#define ZB_MINIMAL_NWK_LEAVE_REJOIN           BIT(5)
#define ZB_MINIMAL_NWK_LEAVE_REQUEST          BIT(6)
#define ZB_MINIMAL_NWK_LEAVE_REMOVE_CHILDREN  BIT(7)
#define ZB_MINIMAL_ZDO_RSP_Q_LEN              3U

static zb_minimal_pending_zdo_rsp_t g_minimal_pending_zdo_rsp_q[ZB_MINIMAL_ZDO_RSP_Q_LEN];
static u8 g_minimal_pending_zdo_rsp_head;
static u8 g_minimal_pending_zdo_rsp_tail;
static u8 g_minimal_pending_zdo_rsp_count;
static zb_minimal_pending_zcl_rsp_t g_minimal_pending_zcl_rsp;
static zb_minimal_pending_leave_t g_minimal_pending_leave;
volatile u32 zb_minimal_zdo_trace[8] = {0x5a444f31U};
volatile u32 zb_minimal_join_filter_trace[8] = {0x4a464c54U};
volatile u32 zb_minimal_join_gate_trace[8] = {0x4a474154U};

static bool zb_minimal_security_key_is_set(const u8 *key)
{
	if (key == NULL) {
		return false;
	}

	for (u8 i = 0U; i < SEC_KEY_LEN; i++) {
		if (key[i] != 0U) {
			return true;
		}
	}

	return false;
}

static u8 *zb_minimal_nwk_key_by_seq(u8 key_seq)
{
	if (key_seq == 0U) {
		key_seq = ss_ib.activeKeySeqNum;
	}

	for (u8 i = 0U; i < SECUR_N_SECUR_MATERIAL; i++) {
		u8 idx = (u8)((ss_ib.activeSecureMaterialIndex + i) & 0x01U);
		u8 *key = ss_ib.nwkSecurMaterialSet[idx].key;

		if ((ss_ib.nwkSecurMaterialSet[idx].keySeqNum == key_seq) &&
		    zb_minimal_security_key_is_set(key)) {
			return key;
		}
	}

	return NULL;
}

static u8 *zb_minimal_active_nwk_key_get(void)
{
	if (ss_ib.activeSecureMaterialIndex >= SECUR_N_SECUR_MATERIAL) {
		return NULL;
	}

	return ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
}

static size_t zb_minimal_build_mac_header(u8 *buf, u16 mac_dst)
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
	COPY_U16TOBUFFER(&buf[idx], mac_dst);
	idx += MAC_SHORT_ADDR_FIELD_LEN;
	COPY_U16TOBUFFER(&buf[idx], g_zbNIB.nwkAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;

	return idx;
}

static size_t zb_minimal_build_nwk_header_type(u8 *buf, u16 nwk_dst, u8 radius, bool security,
					       u8 frame_type, u32 *frame_counter_out)
{
	u16 fc = 0U;
	size_t idx = 0U;

	fc |= (u16)(frame_type & 0x03U);
	fc |= (u16)(0x02U << 2);
	if (security) {
		fc |= BIT(9);
	}

	COPY_U16TOBUFFER(&buf[idx], fc);
	idx += 2U;
	COPY_U16TOBUFFER(&buf[idx], nwk_dst);
	idx += 2U;
	COPY_U16TOBUFFER(&buf[idx], g_zbNIB.nwkAddr);
	idx += 2U;
	buf[idx++] = (radius != 0U) ? radius : 30U;
	buf[idx++] = g_zbNIB.seqNum++;

	if (security) {
		u32 frame_counter = ss_ib.outgoingFrameCounter++;

		buf[idx++] = ZB_MINIMAL_NWK_SEC_CTRL;
		COPY_U32TOBUFFER(&buf[idx], frame_counter);
		idx += 4U;
		memcpy(&buf[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
		idx += sizeof(addrExt_t);
		buf[idx++] = ss_ib.activeKeySeqNum;

		if (frame_counter_out != NULL) {
			*frame_counter_out = frame_counter;
		}
	} else if (frame_counter_out != NULL) {
		*frame_counter_out = 0U;
	}

	return idx;
}

static size_t zb_minimal_build_nwk_header(u8 *buf, u16 nwk_dst, u8 radius, bool security,
					  u32 *frame_counter_out)
{
	return zb_minimal_build_nwk_header_type(buf, nwk_dst, radius, security,
						FRAME_TYPE_DATA, frame_counter_out);
}

static size_t zb_minimal_build_aps_ack_header(u8 *buf, const zb_minimal_aps_frame_t *aps)
{
	u8 fc = 0x02U;
	size_t idx = 0U;

	if (aps->frame_type == 1U) {
		fc |= BIT(4);
	}

	buf[idx++] = fc;
	if (aps->frame_type != 1U) {
		buf[idx++] = aps->src_ep;
		COPY_U16TOBUFFER(&buf[idx], aps->cluster_id);
		idx += 2U;
		COPY_U16TOBUFFER(&buf[idx], aps->profile_id);
		idx += 2U;
		buf[idx++] = aps->dst_ep;
	}
	buf[idx++] = aps->aps_counter;

	return idx;
}

static int zb_minimal_send_aps_ack(const zb_minimal_nwk_frame_t *nwk, const zb_minimal_aps_frame_t *aps)
{
	u8 frame[127];
	u8 nonce[13];
	u8 *nwk_key;
	size_t idx = 0U;
	size_t nwk_hdr_len;
	size_t aps_hdr_len;
	size_t aps_hdr_idx;
	u32 nwk_frame_counter = 0U;
	u8 enc_len;
	u8 attempt;
	int rc = -EINVAL;

	if ((nwk == NULL) || (aps == NULL)) {
		return -EINVAL;
	}
	if (aps->delivery_mode != 0U || ZB_NWK_IS_ADDRESS_BROADCAST(nwk->src_addr)) {
		return -ENOTSUP;
	}
	if (ss_ib.securityLevel == 0U) {
		return -EACCES;
	}

	nwk_key = zb_minimal_active_nwk_key_get();
	if (nwk_key == NULL) {
		return -EACCES;
	}

	idx += zb_minimal_build_mac_header(&frame[idx], nwk->src_addr);
	nwk_hdr_len = zb_minimal_build_nwk_header(&frame[idx], nwk->src_addr, 30U, true,
						      &nwk_frame_counter);
	idx += nwk_hdr_len;
	aps_hdr_idx = idx;
	aps_hdr_len = zb_minimal_build_aps_ack_header(&frame[idx], aps);
	idx += aps_hdr_len;

	memcpy(nonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
	COPY_U32TOBUFFER(&nonce[8], nwk_frame_counter);
	nonce[12] = ZB_MINIMAL_NWK_SEC_CTRL;
	enc_len = zb_minimal_ccm_encrypt_auth(nwk_key, nonce, ZB_MINIMAL_NWK_MIC_LEN,
						      &frame[aps_hdr_idx - nwk_hdr_len],
						      (u8)nwk_hdr_len, &frame[aps_hdr_idx],
						      (u8)aps_hdr_len, &frame[aps_hdr_idx + aps_hdr_len]);
	if (enc_len != (u8)(aps_hdr_len + ZB_MINIMAL_NWK_MIC_LEN)) {
		return -EBADMSG;
	}

	frame[aps_hdr_idx - nwk_hdr_len + 8U] = ZB_MINIMAL_NWK_SEC_CTRL_WIRE;
	idx = aps_hdr_idx + enc_len;
	/* Frame counter persisted at safe sync points only — NVS write in the
	 * TX hot path can stall the Zigbee thread mid-interview on TLSR8258. */

	for (attempt = 0U; attempt < ZB_MINIMAL_TX_RETRIES; attempt++) {
		rc = zb_platform_radio_send_raw_psdu(frame, (u8)idx);
		if (rc >= 0) {
			return 0;
		}
		if (rc != -EBUSY && rc != -EAGAIN) {
			break;
		}
		k_busy_wait(ZB_MINIMAL_TX_RETRY_US);
	}

	return rc;
}

static bool zb_minimal_parse_mac_frame(const u8 *psdu, u8 len, zb_minimal_mac_frame_t *frame)
{
	u16 frame_ctrl;
	u8 header_len;
	u8 payload_len;

	if ((psdu == NULL) || (frame == NULL) || (len < (MAC_MIN_HDR_LEN + MAC_FCS_FIELD_LEN))) {
		return false;
	}

	frame_ctrl = zb_u16_from_le(psdu);
	header_len = tl_zbMacHdrSize(frame_ctrl);
	payload_len = (u8)(len - MAC_FCS_FIELD_LEN);
	if (header_len > payload_len) {
		return false;
	}

	memset(frame, 0, sizeof(*frame));
	frame->frame_ctrl = frame_ctrl;
	frame->header_len = header_len;
	frame->payload_len = (u8)(payload_len - header_len);
	frame->payload = &psdu[header_len];
	return true;
}

static bool zb_minimal_parse_mac_addr_info(const u8 *psdu, u8 len, zb_minimal_mac_addr_info_t *info)
{
	u16 frame_ctrl;
	u8 idx = MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN;
	u8 dst_mode;
	u8 src_mode;
	u16 dst_pan = MAC_INVALID_PANID;

	if ((psdu == NULL) || (info == NULL) || (len < idx)) {
		return false;
	}

	memset(info, 0, sizeof(*info));
	frame_ctrl = zb_u16_from_le(psdu);
	info->frame_ctrl = frame_ctrl;
	dst_mode = (u8)((frame_ctrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	src_mode = (u8)((frame_ctrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	if (dst_mode != ZB_ADDR_NO_ADDR) {
		if ((u16)(idx + MAC_PAN_ID_FIELD_LEN) > len) {
			return false;
		}
		dst_pan = zb_u16_from_le(&psdu[idx]);
		idx += MAC_PAN_ID_FIELD_LEN;

		if (dst_mode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
			if ((u16)(idx + MAC_SHORT_ADDR_FIELD_LEN) > len) {
				return false;
			}
			info->dst_short_valid = true;
			info->dst_short_addr = zb_u16_from_le(&psdu[idx]);
			idx += MAC_SHORT_ADDR_FIELD_LEN;
		} else if (dst_mode == ZB_ADDR_64BIT_DEV) {
			if ((u16)(idx + MAC_EXT_ADDR_FIELD_LEN) > len) {
				return false;
			}
			info->dst_ext_valid = true;
			memcpy(info->dst_ext_addr, &psdu[idx], sizeof(info->dst_ext_addr));
			idx += MAC_EXT_ADDR_FIELD_LEN;
		} else {
			return false;
		}
	}

	if (src_mode == ZB_ADDR_NO_ADDR) {
		return true;
	}

	if ((frame_ctrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
		if ((u16)(idx + MAC_PAN_ID_FIELD_LEN) > len) {
			return false;
		}
		idx += MAC_PAN_ID_FIELD_LEN;
	} else {
		ARG_UNUSED(dst_pan);
	}

	if (src_mode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
		if ((u16)(idx + MAC_SHORT_ADDR_FIELD_LEN) > len) {
			return false;
		}
		info->src_short_valid = true;
		info->src_short_addr = zb_u16_from_le(&psdu[idx]);
		idx += MAC_SHORT_ADDR_FIELD_LEN;
	} else if (src_mode == ZB_ADDR_64BIT_DEV) {
		if ((u16)(idx + MAC_EXT_ADDR_FIELD_LEN) > len) {
			return false;
		}
		info->src_ext_valid = true;
		memcpy(info->src_ext_addr, &psdu[idx], sizeof(info->src_ext_addr));
		idx += MAC_EXT_ADDR_FIELD_LEN;
	} else {
		return false;
	}

	return true;
}

static bool zb_minimal_interview_frame_relevant(const u8 *psdu, u8 len)
{
	zb_minimal_mac_addr_info_t info;
	bool dst_match = false;
	bool src_match = false;

	if (!zb_minimal_parse_mac_addr_info(psdu, len, &info)) {
		zb_minimal_join_filter_trace[1] = 0xf0010000U | len;
		return false;
	}
	zb_minimal_join_filter_trace[1] =
		((u32)(info.dst_short_valid ? 1U : 0U) << 31) |
		((u32)(info.dst_ext_valid ? 1U : 0U) << 30) |
		((u32)(info.src_short_valid ? 1U : 0U) << 29) |
		((u32)(info.src_ext_valid ? 1U : 0U) << 28) |
		((u32)info.dst_short_addr << 16) | info.src_short_addr;

	if (info.dst_short_valid) {
		dst_match = (info.dst_short_addr == g_zbMacPib.shortAddress);
	} else if (info.dst_ext_valid) {
		dst_match = memcmp(info.dst_ext_addr, g_zbMacPib.extAddress, sizeof(addrExt_t)) == 0;
	}

	if (!dst_match) {
		zb_minimal_join_filter_trace[2] = 0xf0020000U | info.dst_short_addr;
		return false;
	}

	if (info.src_short_valid && g_zbMacPib.coordShortAddress < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED) {
		src_match = (info.src_short_addr == g_zbMacPib.coordShortAddress);
	} else if (info.src_ext_valid &&
		   !ZB_IEEE_ADDR_IS_ZERO(g_zbMacPib.coordExtAddress) &&
		   !ZB_IEEE_ADDR_IS_INVALID(g_zbMacPib.coordExtAddress)) {
		src_match = memcmp(info.src_ext_addr, g_zbMacPib.coordExtAddress,
				   sizeof(addrExt_t)) == 0;
	} else {
		src_match = false;
	}
	zb_minimal_join_filter_trace[3] =
		((u32)(src_match ? 1U : 0U) << 24) | g_zbMacPib.coordShortAddress;

	if (!src_match) {
		if (!aps_ib.aps_authenticated &&
		    info.dst_ext_valid &&
		    memcmp(info.dst_ext_addr, g_zbMacPib.extAddress, sizeof(addrExt_t)) == 0 &&
		    g_zbMacPib.coordShortAddress >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED &&
		    (ZB_IEEE_ADDR_IS_ZERO(g_zbMacPib.coordExtAddress) ||
		     ZB_IEEE_ADDR_IS_INVALID(g_zbMacPib.coordExtAddress))) {
			return true;
		}
		zb_minimal_join_filter_trace[4] = 0xf0030000U | info.src_short_addr;
		return false;
	}

	zb_minimal_join_filter_trace[4] = 0xf0040000U | info.src_short_addr;

	return src_match;
}

static bool zb_minimal_parse_nwk_frame(const u8 *payload, u8 payload_len, zb_minimal_nwk_frame_t *frame)
{
	u16 frame_ctrl;
	u8 idx = 0U;

	if ((payload == NULL) || (frame == NULL) || (payload_len < 8U)) {
		return false;
	}

	frame_ctrl = zb_u16_from_le(payload);
	idx += 2U;

	memset(frame, 0, sizeof(*frame));
	frame->frame_ctrl = frame_ctrl;
	frame->frame_type = (u8)(frame_ctrl & 0x0003U);
	frame->multicast = (frame_ctrl & BIT(8)) != 0U;
	frame->security = (frame_ctrl & BIT(9)) != 0U;
	frame->source_route = (frame_ctrl & BIT(10)) != 0U;
	frame->dst_ieee = (frame_ctrl & BIT(11)) != 0U;
	frame->src_ieee = (frame_ctrl & BIT(12)) != 0U;

	frame->dst_addr = zb_u16_from_le(&payload[idx]);
	idx += 2U;
	frame->src_addr = zb_u16_from_le(&payload[idx]);
	idx += 2U;

	/* radius + sequence number */
	idx += 2U;

	if (frame->dst_ieee) {
		idx += 8U;
	}
	if (frame->src_ieee) {
		idx += 8U;
	}
	if (frame->multicast) {
		idx += 1U;
	}
	if (frame->source_route) {
		u8 relay_count;

		if (idx + 2U > payload_len) {
			return false;
		}
		relay_count = payload[idx];
		idx += 2U;
		if (idx + (u8)(relay_count * 2U) > payload_len) {
			return false;
		}
		idx += (u8)(relay_count * 2U);
	}

	if (frame->security) {
		if (idx + ZB_MINIMAL_NWK_AUX_HDR_LEN > payload_len) {
			return false;
		}

		frame->security_control = payload[idx];
		frame->frame_counter = zb_u32_from_le(&payload[idx + 1U]);
		memcpy(frame->security_src_ext, &payload[idx + 5U], sizeof(frame->security_src_ext));
		frame->key_seq = payload[idx + 13U];
		frame->mic_len = ZB_MINIMAL_NWK_MIC_LEN;
		idx += ZB_MINIMAL_NWK_AUX_HDR_LEN;
	}

	if (idx > payload_len) {
		return false;
	}

	frame->header_len = idx;
	frame->payload_len = (u8)(payload_len - idx);
	frame->payload = &payload[idx];
	return true;
}

static bool zb_minimal_decrypt_nwk_payload(u8 *nwk_psdu, u8 nwk_len, zb_minimal_nwk_frame_t *frame)
{
	u8 aad[ZB_NWK_FRAME_HEADER];
	u8 nonce[13];
	u8 *key;
	u8 *cipher;
	u8 *mic;
	u8 cipher_len;
	u8 sec_ctrl_idx;

	if ((nwk_psdu == NULL) || (frame == NULL) || !frame->security) {
		return false;
	}

	if ((frame->header_len > nwk_len) || (frame->header_len > sizeof(aad)) ||
	    (frame->payload_len <= frame->mic_len)) {
		return false;
	}

	key = zb_minimal_nwk_key_by_seq(frame->key_seq);
	if (key == NULL) {
		LOG_DBG("joined RX: no nwk key for seq=%u", frame->key_seq);
		return false;
	}

	memcpy(aad, nwk_psdu, frame->header_len);
	sec_ctrl_idx = (u8)(frame->header_len - ZB_MINIMAL_NWK_AUX_HDR_LEN);
	aad[sec_ctrl_idx] = (aad[sec_ctrl_idx] & 0xF8U) | 0x05U;

	memcpy(nonce, frame->security_src_ext, sizeof(frame->security_src_ext));
	COPY_U32TOBUFFER(&nonce[8], frame->frame_counter);
	nonce[12] = aad[sec_ctrl_idx];

	cipher = &nwk_psdu[frame->header_len];
	cipher_len = (u8)(frame->payload_len - frame->mic_len);
	mic = &cipher[cipher_len];

	if (!zb_minimal_ccm_decrypt_auth(key, nonce, frame->mic_len, cipher, cipher_len, aad,
					 frame->header_len, mic)) {
		LOG_DBG("joined RX: nwk auth failed src=0x%04x key_seq=%u", frame->src_addr,
			frame->key_seq);
		return false;
	}

	frame->payload = cipher;
	frame->payload_len = cipher_len;
	return true;
}

static bool zb_minimal_parse_aps_frame(const u8 *payload, u8 payload_len, zb_minimal_aps_frame_t *frame)
{
	u8 idx = 0U;
	u8 header_len;
	u8 sec_ctrl_idx = 0U;
	u8 sec_ctrl;
	u8 fc;
	u8 frame_type;
	u8 delivery_mode;

	if ((payload == NULL) || (frame == NULL) || (payload_len < 2U)) {
		return false;
	}

	memset(frame, 0, sizeof(*frame));
	fc = payload[idx++];
	frame_type = (u8)(fc & 0x03U);
	delivery_mode = (u8)((fc >> 2) & 0x03U);

	if ((frame_type != 0U && frame_type != 1U) || delivery_mode > 0x02U) {
		return false;
	}

	frame->frame_control = fc;
	frame->frame_type = frame_type;
	frame->delivery_mode = delivery_mode;
	frame->security = (fc & BIT(5)) != 0U;
	frame->ack_req = (fc & BIT(6)) != 0U;
	frame->extended_hdr = (fc & BIT(7)) != 0U;

	if (frame->extended_hdr) {
		return false;
	}

	if (frame_type == 0U) {
		if (payload_len < 8U) {
			return false;
		}

		frame->dst_ep = payload[idx++];
		frame->cluster_id = zb_u16_from_le(&payload[idx]);
		idx += 2U;
		frame->profile_id = zb_u16_from_le(&payload[idx]);
		idx += 2U;
		frame->src_ep = payload[idx++];
		frame->counter = payload[idx++];
	} else {
		frame->counter = payload[idx++];
	}

	header_len = idx;
	frame->aps_counter = frame->counter;

	if (frame->security) {
		if (idx + 5U > payload_len) {
			return false;
		}

		sec_ctrl_idx = idx;
		sec_ctrl = payload[idx++];
		frame->security_control = sec_ctrl;
		frame->key_id = (u8)((sec_ctrl >> 3) & 0x03U);
		frame->frame_counter = zb_u32_from_le(&payload[idx]);
		idx += 4U;
		if ((sec_ctrl & BIT(5)) != 0U) {
			if (idx + sizeof(addrExt_t) > payload_len) {
				return false;
			}
			memcpy(frame->security_src_ext, &payload[idx], sizeof(addrExt_t));
			idx += sizeof(addrExt_t);
		} else {
			const u8 *security_src_ext = zb_minimal_aps_security_src_fallback_get();

			if (security_src_ext == NULL) {
				LOG_DBG("joined RX: missing APS security source IEEE");
				return false;
			}

			memcpy(frame->security_src_ext, security_src_ext,
			       sizeof(frame->security_src_ext));
		}
		if (frame->key_id == SS_SECUR_NWK_KEY) {
			if (idx + 1U > payload_len) {
				return false;
			}
			idx += 1U;
		}
		frame->mic_len = ZB_MINIMAL_APS_MIC_LEN;
	}

	if (idx > payload_len) {
		return false;
	}

	frame->header_len = header_len;
	frame->payload_len = (u8)(payload_len - idx);
	frame->payload = &payload[idx];
	if (frame->security) {
		if (frame->payload_len <= frame->mic_len) {
			return false;
		}
		ARG_UNUSED(sec_ctrl_idx);
	}
	return true;
}

static bool zb_minimal_decrypt_aps_payload(u8 *aps_psdu, u8 aps_len, zb_minimal_aps_frame_t *frame)
{
	u8 aad[24];
	u8 nonce[13];
	u8 *key = NULL;
	u8 *cipher;
	u8 cipher_shadow[80];
	u8 hashed_key[SEC_KEY_LEN];
	u8 *mic;
	u8 cipher_len;
	u8 aad_len;
	u8 sec_ctrl_idx;
	bool transport_key_sec = false;

	if ((aps_psdu == NULL) || (frame == NULL) || !frame->security) {
		return false;
	}

	if ((frame->payload_len <= frame->mic_len) || (aps_len < frame->header_len)) {
		return false;
	}

	switch (frame->key_id) {
	case SS_SECUR_KEY_TRANSPORT_KEY:
	case SS_SECUR_KEY_LOAD_KEY:
		key = ss_ib.tcLinkKey;
		transport_key_sec = true;
		break;
	case SS_SECUR_NWK_KEY:
		key = zb_minimal_nwk_key_by_seq(ss_ib.activeKeySeqNum);
		break;
	default:
		break;
	}

	if (key == NULL) {
		LOG_DBG("joined RX: no APS key for key_id=%u", frame->key_id);
		return false;
	}

	if (transport_key_sec) {
		u8 pad = (frame->key_id == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;

		if (ss_keyHash(&pad, key, hashed_key) == RET_OK) {
			key = hashed_key;
		}
	}

	sec_ctrl_idx = frame->header_len;
	if (frame->frame_type == 0U) {
		sec_ctrl_idx = 8U;
	} else if (frame->frame_type == 1U) {
		sec_ctrl_idx = 2U;
	}
	if (sec_ctrl_idx >= aps_len) {
		return false;
	}

	memcpy(aad, aps_psdu, (size_t)(frame->payload - aps_psdu));
	aad[sec_ctrl_idx] = (aad[sec_ctrl_idx] & 0xF8U) | 0x05U;

	memcpy(nonce, frame->security_src_ext, sizeof(frame->security_src_ext));
	COPY_U32TOBUFFER(&nonce[8], frame->frame_counter);
	nonce[12] = aad[sec_ctrl_idx];

	cipher = (u8 *)frame->payload;
	aad_len = (u8)(frame->payload - aps_psdu);
	cipher_len = (u8)(frame->payload_len - frame->mic_len);
	mic = &cipher[cipher_len];

	if (cipher_len > sizeof(cipher_shadow)) {
		LOG_DBG("joined RX: APS cipher too large (%u)", cipher_len);
		return false;
	}

	memcpy(cipher_shadow, cipher, cipher_len);
	if (!zb_minimal_ccm_decrypt_auth(key, nonce, frame->mic_len, cipher_shadow, cipher_len, aad,
					 aad_len, mic)) {
		if (transport_key_sec) {
		}
		LOG_DBG("joined RX: APS auth failed key_id=%u", frame->key_id);
		return false;
	}

	memcpy(cipher, cipher_shadow, cipher_len);
	frame->payload = cipher;
	frame->payload_len = cipher_len;
	return true;
}

static u8 zb_minimal_send_zdo_response(u16 dst_nwk_addr, u16 cluster_id, const u8 *payload, u16 payload_len)
{
	epInfo_t dst;
	u8 aps_cnt = 0U;

	TL_SETSTRUCTCONTENT(dst, 0);
	dst.profileId = ZDO_PROFILE_ID;
	dst.dstEp = ZDO_EP;
	dst.radius = 30U;
	dst.txOptions = APS_TX_OPT_ACK_TX;
	dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst.dstAddr.shortAddr = dst_nwk_addr;
	return af_dataSend(ZDO_EP, &dst, cluster_id, payload_len, (u8 *)payload, &aps_cnt);
}

static void zb_minimal_zdo_response_task(void *arg)
{
	zb_minimal_pending_zdo_rsp_t rsp;

	ARG_UNUSED(arg);

	while (g_minimal_pending_zdo_rsp_count != 0U) {
		memcpy(&rsp, &g_minimal_pending_zdo_rsp_q[g_minimal_pending_zdo_rsp_tail],
		       sizeof(rsp));
		memset(&g_minimal_pending_zdo_rsp_q[g_minimal_pending_zdo_rsp_tail], 0,
		       sizeof(g_minimal_pending_zdo_rsp_q[g_minimal_pending_zdo_rsp_tail]));
		g_minimal_pending_zdo_rsp_tail =
			(u8)((g_minimal_pending_zdo_rsp_tail + 1U) % ZB_MINIMAL_ZDO_RSP_Q_LEN);
		g_minimal_pending_zdo_rsp_count--;

		u8 status = zb_minimal_send_zdo_response(rsp.dst_nwk_addr, rsp.cluster_id,
						 rsp.payload, rsp.payload_len);
		zb_minimal_zdo_trace[5] = ((u32)status << 24) | ((u32)rsp.payload_len << 16) |
					  rsp.cluster_id;
		if (status == APS_STATUS_SUCCESS) {
			tl_zbNwkEdMinimalInterviewPollStart(0U, 0U);
			LOG_INF("minimal ZDO rsp sent dst=0x%04x cluster=0x%04x len=%u",
				rsp.dst_nwk_addr, rsp.cluster_id, rsp.payload_len);
		} else {
			LOG_WRN("minimal ZDO rsp failed dst=0x%04x cluster=0x%04x len=%u",
				rsp.dst_nwk_addr, rsp.cluster_id, rsp.payload_len);
		}
	}
}

static bool zb_minimal_queue_zdo_response(u16 dst_nwk_addr, u16 cluster_id,
						 const u8 *payload, u16 payload_len)
{
	zb_minimal_pending_zdo_rsp_t *rsp;

	if (payload == NULL || payload_len > sizeof(g_minimal_pending_zdo_rsp_q[0].payload)) {
		return false;
	}

	if (g_minimal_pending_zdo_rsp_count >= ZB_MINIMAL_ZDO_RSP_Q_LEN) {
		LOG_WRN("minimal ZDO rsp queue full dst=0x%04x cluster=0x%04x",
			dst_nwk_addr, cluster_id);
		zb_minimal_zdo_trace[4]++;
		return false;
	}

	rsp = &g_minimal_pending_zdo_rsp_q[g_minimal_pending_zdo_rsp_head];
	memset(rsp, 0, sizeof(*rsp));
	rsp->pending = true;
	rsp->dst_nwk_addr = dst_nwk_addr;
	rsp->cluster_id = cluster_id;
	rsp->payload_len = payload_len;
	memcpy(rsp->payload, payload, payload_len);
	g_minimal_pending_zdo_rsp_head =
		(u8)((g_minimal_pending_zdo_rsp_head + 1U) % ZB_MINIMAL_ZDO_RSP_Q_LEN);
	g_minimal_pending_zdo_rsp_count++;
	zb_minimal_zdo_trace[4] = ((u32)g_minimal_pending_zdo_rsp_count << 24) |
				  ((u32)payload_len << 16) | cluster_id;

	if (TL_SCHEDULE_TASK(zb_minimal_zdo_response_task, NULL) != RET_OK) {
		g_minimal_pending_zdo_rsp_head =
			(u8)((g_minimal_pending_zdo_rsp_head + ZB_MINIMAL_ZDO_RSP_Q_LEN - 1U) %
			     ZB_MINIMAL_ZDO_RSP_Q_LEN);
		memset(&g_minimal_pending_zdo_rsp_q[g_minimal_pending_zdo_rsp_head], 0,
		       sizeof(g_minimal_pending_zdo_rsp_q[g_minimal_pending_zdo_rsp_head]));
		g_minimal_pending_zdo_rsp_count--;
		zb_minimal_zdo_trace[7]++;
		return false;
	}

	return true;
}

#define ZB_MINIMAL_HA_PROFILE_ID              0x0104U
#define ZB_MINIMAL_ZCL_BASIC_CLUSTER_ID       0x0000U
#define ZB_MINIMAL_ZCL_FRAME_GLOBAL_RSP       0x18U
#define ZB_MINIMAL_ZCL_CMD_READ               0x00U
#define ZB_MINIMAL_ZCL_CMD_READ_RSP           0x01U
#define ZB_MINIMAL_ZCL_STATUS_SUCCESS         0x00U
#define ZB_MINIMAL_ZCL_STATUS_UNSUPPORTED_ATTR 0x86U
#define ZB_MINIMAL_ZCL_TYPE_UINT8             0x20U
#define ZB_MINIMAL_ZCL_TYPE_ENUM8             0x30U
#define ZB_MINIMAL_ZCL_TYPE_CHAR_STR          0x42U
#define ZB_MINIMAL_ZCL_BASIC_ZCL_VERSION      0x0000U
#define ZB_MINIMAL_ZCL_BASIC_APP_VERSION      0x0001U
#define ZB_MINIMAL_ZCL_BASIC_STACK_VERSION    0x0002U
#define ZB_MINIMAL_ZCL_BASIC_HW_VERSION       0x0003U
#define ZB_MINIMAL_ZCL_BASIC_MFR_NAME         0x0004U
#define ZB_MINIMAL_ZCL_BASIC_MODEL_ID         0x0005U
#define ZB_MINIMAL_ZCL_BASIC_POWER_SOURCE     0x0007U

/*
 * Weak identity hooks.  The zigbee_shell sample overrides these via
 * app_profile.c so that the fallback interview path answers with the
 * same strings as the real ZCL attribute table.
 */
const char *zb_platform_app_basic_mfr_name(void) __weak;
const char *zb_platform_app_basic_mfr_name(void)
{
	return "Telink";
}

const char *zb_platform_app_basic_model_id(void) __weak;
const char *zb_platform_app_basic_model_id(void)
{
	return "tlsr8258-zigbee-shell";
}

static u16 zb_minimal_zcl_put_basic_attr(u8 *buf, u16 pos, u16 max_len, u16 attr_id)
{
	const char *str = NULL;
	u8 type = 0U;
	u8 value = 0U;

	if ((buf == NULL) || (pos + 3U > max_len)) {
		return pos;
	}

	COPY_U16TOBUFFER(&buf[pos], attr_id);
	pos += 2U;

	switch (attr_id) {
	case ZB_MINIMAL_ZCL_BASIC_ZCL_VERSION:
		type = ZB_MINIMAL_ZCL_TYPE_UINT8;
		value = 3U;
		break;
	case ZB_MINIMAL_ZCL_BASIC_APP_VERSION:
	case ZB_MINIMAL_ZCL_BASIC_STACK_VERSION:
	case ZB_MINIMAL_ZCL_BASIC_HW_VERSION:
		type = ZB_MINIMAL_ZCL_TYPE_UINT8;
		value = 1U;
		break;
	case ZB_MINIMAL_ZCL_BASIC_POWER_SOURCE:
		type = ZB_MINIMAL_ZCL_TYPE_ENUM8;
		value = 3U;
		break;
	case ZB_MINIMAL_ZCL_BASIC_MFR_NAME:
		type = ZB_MINIMAL_ZCL_TYPE_CHAR_STR;
		str = zb_platform_app_basic_mfr_name();
		break;
	case ZB_MINIMAL_ZCL_BASIC_MODEL_ID:
		type = ZB_MINIMAL_ZCL_TYPE_CHAR_STR;
		str = zb_platform_app_basic_model_id();
		break;
	default:
		buf[pos++] = ZB_MINIMAL_ZCL_STATUS_UNSUPPORTED_ATTR;
		return pos;
	}

	buf[pos++] = ZB_MINIMAL_ZCL_STATUS_SUCCESS;
	buf[pos++] = type;
	if (str != NULL) {
		u8 len = (u8)strlen(str);

		if (pos + 1U + len > max_len) {
			return pos;
		}
		buf[pos++] = len;
		memcpy(&buf[pos], str, len);
		pos += len;
	} else {
		if (pos + 1U > max_len) {
			return pos;
		}
		buf[pos++] = value;
	}

	return pos;
}

static u8 zb_minimal_send_zcl_response(const zb_minimal_pending_zcl_rsp_t *rsp)
{
	epInfo_t dst;
	u8 aps_cnt = 0U;

	if (rsp == NULL) {
		return APS_STATUS_INVALID_PARAMETER;
	}

	TL_SETSTRUCTCONTENT(dst, 0);
	dst.profileId = rsp->profile_id;
	dst.dstEp = rsp->dst_ep;
	dst.radius = 30U;
	dst.txOptions = APS_TX_OPT_ACK_TX;
	dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst.dstAddr.shortAddr = rsp->dst_nwk_addr;
	return af_dataSend(rsp->src_ep, &dst, rsp->cluster_id, rsp->payload_len,
			   (u8 *)rsp->payload, &aps_cnt);
}

static void zb_minimal_zcl_response_task(void *arg)
{
	zb_minimal_pending_zcl_rsp_t rsp;

	ARG_UNUSED(arg);

	if (!g_minimal_pending_zcl_rsp.pending) {
		return;
	}

	memcpy(&rsp, &g_minimal_pending_zcl_rsp, sizeof(rsp));
	memset(&g_minimal_pending_zcl_rsp, 0, sizeof(g_minimal_pending_zcl_rsp));
	if (zb_minimal_send_zcl_response(&rsp) == APS_STATUS_SUCCESS) {
		tl_zbNwkEdMinimalInterviewPollStart(0U, 0U);
	}
}

static bool zb_minimal_queue_zcl_response(u16 dst_nwk_addr, const zb_minimal_aps_frame_t *aps,
					  const u8 *payload, u16 payload_len)
{
	if ((aps == NULL) || (payload == NULL) ||
	    payload_len > sizeof(g_minimal_pending_zcl_rsp.payload)) {
		return false;
	}

	g_minimal_pending_zcl_rsp.pending = true;
	g_minimal_pending_zcl_rsp.dst_nwk_addr = dst_nwk_addr;
	g_minimal_pending_zcl_rsp.profile_id = aps->profile_id;
	g_minimal_pending_zcl_rsp.cluster_id = aps->cluster_id;
	g_minimal_pending_zcl_rsp.src_ep = aps->dst_ep;
	g_minimal_pending_zcl_rsp.dst_ep = aps->src_ep;
	g_minimal_pending_zcl_rsp.payload_len = payload_len;
	memcpy(g_minimal_pending_zcl_rsp.payload, payload, payload_len);

	if (TL_SCHEDULE_TASK(zb_minimal_zcl_response_task, NULL) != RET_OK) {
		memset(&g_minimal_pending_zcl_rsp, 0, sizeof(g_minimal_pending_zcl_rsp));
		return false;
	}

	return true;
}

static bool zb_minimal_handle_zcl_basic_read(u16 src_nwk_addr, const zb_minimal_aps_frame_t *aps)
{
	u8 payload[64];
	u16 pos = 0U;
	u16 in_pos;

	if ((aps == NULL) || (aps->profile_id != ZB_MINIMAL_HA_PROFILE_ID) ||
	    (aps->cluster_id != ZB_MINIMAL_ZCL_BASIC_CLUSTER_ID) ||
	    (aps->dst_ep == ZDO_EP) || (aps->payload_len < 3U)) {
		return false;
	}
	if ((aps->payload[0] & 0x07U) != 0U || aps->payload[2] != ZB_MINIMAL_ZCL_CMD_READ) {
		return false;
	}

	payload[pos++] = ZB_MINIMAL_ZCL_FRAME_GLOBAL_RSP;
	payload[pos++] = aps->payload[1];
	payload[pos++] = ZB_MINIMAL_ZCL_CMD_READ_RSP;

	in_pos = 3U;
	while ((in_pos + 1U < aps->payload_len) && (pos + 3U < sizeof(payload))) {
		u16 attr_id = zb_u16_from_le(&aps->payload[in_pos]);

		in_pos += 2U;
		pos = zb_minimal_zcl_put_basic_attr(payload, pos, sizeof(payload), attr_id);
	}

	return zb_minimal_queue_zcl_response(src_nwk_addr, aps, payload, pos);
}

static int zb_minimal_send_nwk_leave_command(u8 options)
{
	u8 frame[127];
	u8 nonce[13];
	u8 *nwk_key;
	size_t idx = 0U;
	size_t nwk_hdr_idx;
	size_t nwk_hdr_len;
	size_t payload_idx;
	u16 parent_addr = g_zbMacPib.coordShortAddress;
	u32 nwk_frame_counter = 0U;
	u8 enc_len;
	u8 attempt;
	int rc = -EINVAL;

	if (!g_zbNwkCtx.joined || parent_addr >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED) {
		return -ENOTCONN;
	}

	nwk_key = zb_minimal_active_nwk_key_get();
	if (nwk_key == NULL || ss_ib.securityLevel == 0U) {
		return -EACCES;
	}

	idx += zb_minimal_build_mac_header(&frame[idx], parent_addr);
	nwk_hdr_idx = idx;
	nwk_hdr_len = zb_minimal_build_nwk_header_type(&frame[idx], parent_addr, 1U, true,
						       FRAME_TYPE_COMMAND,
						       &nwk_frame_counter);
	idx += nwk_hdr_len;
	payload_idx = idx;
	frame[idx++] = NWK_CMD_LEAVE;
	frame[idx++] = options;

	memcpy(nonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
	COPY_U32TOBUFFER(&nonce[8], nwk_frame_counter);
	nonce[12] = ZB_MINIMAL_NWK_SEC_CTRL;
	enc_len = zb_minimal_ccm_encrypt_auth(nwk_key, nonce, ZB_MINIMAL_NWK_MIC_LEN,
					      &frame[nwk_hdr_idx], (u8)nwk_hdr_len,
					      &frame[payload_idx],
					      (u8)(idx - payload_idx), &frame[idx]);
	if (enc_len != (u8)((idx - payload_idx) + ZB_MINIMAL_NWK_MIC_LEN)) {
		return -EBADMSG;
	}

	frame[nwk_hdr_idx + 8U] = ZB_MINIMAL_NWK_SEC_CTRL_WIRE;
	idx = payload_idx + enc_len;
	/* Frame counter persisted at safe sync points only — see note above. */

	for (attempt = 0U; attempt < ZB_MINIMAL_TX_RETRIES; attempt++) {
		rc = zb_platform_radio_send_raw_psdu(frame, (u8)idx);
		if (rc >= 0) {
			return 0;
		}
		if (rc != -EBUSY && rc != -EAGAIN) {
			break;
		}
		k_busy_wait(ZB_MINIMAL_TX_RETRY_US);
	}

	return rc;
}

static void zb_minimal_leave_task(void *arg)
{
	zb_minimal_pending_leave_t leave;
	u8 rsp[2];
	int rc;
	bool restart_rejoin;

	ARG_UNUSED(arg);

	if (!g_minimal_pending_leave.pending) {
		return;
	}

	memcpy(&leave, &g_minimal_pending_leave, sizeof(leave));
	memset(&g_minimal_pending_leave, 0, sizeof(g_minimal_pending_leave));

	rc = 0;
	restart_rejoin = false;
	if (leave.send_leave_command) {
		rc = zb_minimal_send_nwk_leave_command(leave.leave_options);
		if (rc < 0) {
			LOG_WRN("minimal leave notification failed: %d", rc);
		}
	}

	if ((leave.leave_options & ZB_MINIMAL_NWK_LEAVE_REJOIN) != 0U) {
		app_bdb_rejoin_callback_trace_put((0x26U << 24) |
						  ((uint32_t)leave.leave_options << 8) |
						  (uint32_t)(leave.send_leave_command ? 1U : 0U));
		zdo_ed_minimal_rejoin_restart_prepare();
		rc = zdo_nwkRejoinStart((u32)1U << g_zbMacPib.phyChannelCur,
					zdo_cfg_attributes.config_nwk_scan_duration);
		app_bdb_rejoin_callback_trace_put((0x27U << 24) | (uint32_t)(uint8_t)rc);
		if (rc == ZDO_SUCCESS) {
			restart_rejoin = true;
		} else {
			LOG_WRN("minimal leave rejoin restart failed: 0x%02x", (u8)rc);
		}
	}

	if (!restart_rejoin) {
		rc = zb_platform_clear_persistent_state();
	}

	if (leave.send_zdo_response) {
		rsp[0] = leave.zdo_seq;
		rsp[1] = (rc == 0) ? ZDO_SUCCESS : ZDO_NOT_SUPPORTED;
		(void)zb_minimal_send_zdo_response(leave.requester_nwk_addr, MGMT_LEAVE_RSP_CLID,
						   rsp, sizeof(rsp));
	}

	if (rc == 0 && !restart_rejoin) {
		zb_platform_app_network_left();
	}
}

static bool zb_minimal_queue_leave(u16 src_nwk_addr, u8 zdo_seq, u8 leave_options,
				       bool send_leave_command, bool send_zdo_response)
{
	g_minimal_pending_leave.pending = true;
	g_minimal_pending_leave.requester_nwk_addr = src_nwk_addr;
	g_minimal_pending_leave.zdo_seq = zdo_seq;
	g_minimal_pending_leave.leave_options = leave_options;
	g_minimal_pending_leave.send_leave_command = send_leave_command;
	g_minimal_pending_leave.send_zdo_response = send_zdo_response;

	if (TL_SCHEDULE_TASK(zb_minimal_leave_task, NULL) != RET_OK) {
		memset(&g_minimal_pending_leave, 0, sizeof(g_minimal_pending_leave));
		return false;
	}

	return true;
}

static bool zb_minimal_handle_nwk_leave_command(const zb_minimal_nwk_frame_t *nwk)
{
	u8 leave_options;
	bool send_leave_command;

	if ((nwk == NULL) || (nwk->payload == NULL) || (nwk->payload_len < 2U) ||
	    (nwk->payload[0] != NWK_CMD_LEAVE)) {
		return false;
	}

	leave_options = nwk->payload[1];
	if (((leave_options & ZB_MINIMAL_NWK_LEAVE_REJOIN) != 0U) && zb_isUnderRejoinMode()) {
		return true;
	}
	send_leave_command = (leave_options & ZB_MINIMAL_NWK_LEAVE_REQUEST) != 0U;
	if (!zb_minimal_queue_leave(nwk->src_addr, 0U, leave_options, send_leave_command, false)) {
		LOG_WRN("minimal NWK leave handling busy");
	}

	return true;
}

static bool zb_minimal_handle_zdo_request(u16 src_nwk_addr, const zb_minimal_aps_frame_t *aps)
{
	u8 payload[64];
	u8 seq;
	u16 nwk_addr_interest;

	if ((aps == NULL) || (aps->profile_id != ZDO_PROFILE_ID) || (aps->dst_ep != ZDO_EP) ||
	    (aps->payload_len < 3U)) {
		return false;
	}
	seq = aps->payload[0];
	nwk_addr_interest = zb_u16_from_le(&aps->payload[1]);
	zb_minimal_zdo_trace[1]++;
	zb_minimal_zdo_trace[2] = ((u32)seq << 24) | ((u32)src_nwk_addr << 8) |
				  (aps->cluster_id & 0xffU);

	switch (aps->cluster_id) {
	case NODE_DESC_REQ_CLID: {
		u16 rsp_len = 4U;

		memset(payload, 0, sizeof(payload));
		payload[0] = seq;
		if (nwk_addr_interest != g_zbNIB.nwkAddr) {
			payload[1] = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ?
					    ZDO_INVALID_REQUEST :
					    ZDO_DEVICE_NOT_FOUND;
			COPY_U16TOBUFFER(&payload[2], nwk_addr_interest);
		} else {
			payload[1] = ZDO_SUCCESS;
			COPY_U16TOBUFFER(&payload[2], g_zbNIB.nwkAddr);
			af_nodeDescriptorCopy((node_descriptor_t *)&payload[rsp_len]);
			rsp_len += sizeof(node_descriptor_t);
		}

		return zb_minimal_queue_zdo_response(src_nwk_addr, NODE_DESC_RSP_CLID, payload,
						       rsp_len);
	}
	case POWER_DESC_REQ_CLID: {
		u16 rsp_len = 4U;

		memset(payload, 0, sizeof(payload));
		payload[0] = seq;
		if (nwk_addr_interest != g_zbNIB.nwkAddr) {
			payload[1] = (af_nodeDevTypeGet() == DEVICE_TYPE_COORDINATOR) ?
					    ZDO_INVALID_REQUEST :
					    ZDO_DEVICE_NOT_FOUND;
			COPY_U16TOBUFFER(&payload[2], nwk_addr_interest);
		} else {
			payload[1] = ZDO_SUCCESS;
			COPY_U16TOBUFFER(&payload[2], g_zbNIB.nwkAddr);
			af_powerDescriptorCopy((power_descriptor_t *)&payload[rsp_len]);
			rsp_len += sizeof(power_descriptor_t);
		}

		return zb_minimal_queue_zdo_response(src_nwk_addr, POWER_DESC_RSP_CLID, payload,
						       rsp_len);
	}
	case ACTIVE_EP_REQ_CLID: {
		af_endpoint_descriptor_t *ep_desc;
		u8 ep_count;
		u8 i;

		if (nwk_addr_interest != g_zbNIB.nwkAddr) {
			return false;
		}

		memset(payload, 0, sizeof(payload));
		payload[0] = seq;
		payload[1] = ZDO_SUCCESS;
		COPY_U16TOBUFFER(&payload[2], g_zbNIB.nwkAddr);

		ep_count = af_availableEpNumGet();
		payload[4] = ep_count;
		ep_desc = af_epDescriptorGet();
		for (i = 0U; i < ep_count && i < MAX_REQUESTED_CLUSTER_NUMBER; i++) {
			payload[5U + i] = ep_desc[i].ep;
		}
		return zb_minimal_queue_zdo_response(src_nwk_addr, ACTIVE_EP_RSP_CLID, payload,
						       (u16)(5U + MIN(ep_count, MAX_REQUESTED_CLUSTER_NUMBER)));
	}
	case SIMPLE_DESC_REQ_CLID: {
		af_simple_descriptor_t *sd;
		u8 simple_len;

		if (aps->payload_len < 4U || nwk_addr_interest != g_zbNIB.nwkAddr) {
			return false;
		}

		zb_minimal_zdo_trace[3] = ((u32)aps->payload[3] << 24) | nwk_addr_interest;
		memset(payload, 0, sizeof(payload));
		payload[0] = seq;
		payload[1] = ZDO_SUCCESS;
		COPY_U16TOBUFFER(&payload[2], g_zbNIB.nwkAddr);

		sd = af_simpleDescGet(aps->payload[3]);
		if (sd == NULL) {
			payload[1] = ZDO_INVALID_EP;
			payload[4] = 0U;
			return zb_minimal_queue_zdo_response(src_nwk_addr, SIMPLE_DESC_RSP_CLID,
							       payload, 5U);
		}

		simple_len = af_simpleDescriptorCopy(&payload[5], sd);
		payload[4] = simple_len;
		zb_minimal_zdo_trace[6] = ((u32)simple_len << 24) |
					  ((u32)sd->app_in_cluster_count << 16) |
					  sd->endpoint;
		return zb_minimal_queue_zdo_response(src_nwk_addr, SIMPLE_DESC_RSP_CLID, payload,
						       (u16)(5U + simple_len));
	}
	case MGMT_LEAVE_REQ_CLID: {
		const u8 *device_addr;
		u8 flags;
		u8 leave_options = 0U;
		bool self_leave;

		if (aps->payload_len < 10U) {
			payload[0] = seq;
			payload[1] = ZDO_INVALID_REQUEST;
			return zb_minimal_queue_zdo_response(src_nwk_addr, MGMT_LEAVE_RSP_CLID,
							     payload, 2U);
		}

		device_addr = &aps->payload[1];
		flags = aps->payload[9];
		self_leave = ZB_IS_64BIT_ADDR_ZERO(device_addr) ||
			     ZB_64BIT_ADDR_CMP(device_addr, g_zbMacPib.extAddress);
		payload[0] = seq;
		if (!self_leave) {
			payload[1] = ZDO_NOT_SUPPORTED;
			return zb_minimal_queue_zdo_response(src_nwk_addr, MGMT_LEAVE_RSP_CLID,
							     payload, 2U);
		}

		if ((flags & ZB_MINIMAL_MGMT_LEAVE_REJOIN) != 0U) {
			leave_options |= ZB_MINIMAL_NWK_LEAVE_REJOIN;
		}
		if ((flags & ZB_MINIMAL_MGMT_LEAVE_REMOVE_CHILDREN) != 0U) {
			leave_options |= ZB_MINIMAL_NWK_LEAVE_REMOVE_CHILDREN;
		}

		if (!zb_minimal_queue_leave(src_nwk_addr, seq, leave_options, true, true)) {
			payload[1] = ZDO_INSUFFICIENT_SPACE;
			return zb_minimal_queue_zdo_response(src_nwk_addr, MGMT_LEAVE_RSP_CLID,
							     payload, 2U);
		}

		return true;
	}
	default:
		return false;
	}
}

static s32 zb_minimal_transport_key_save_timer(void *arg)
{
	ARG_UNUSED(arg);
	zb_info_save(NULL);
	return -1;
}

static bool zb_minimal_handle_transport_key(const zb_minimal_aps_frame_t *aps)
{
	ss_material_set_t *material;
	const u8 *payload;
	u8 key_seq;
	const u8 *dst_ieee;
	const u8 *src_ieee;

	if ((aps == NULL) || (aps->frame_type != 1U) || (aps->payload_len < 27U)) {
		return false;
	}

	payload = aps->payload;
	if (aps->security &&
	    (aps->key_id == SS_SECUR_KEY_TRANSPORT_KEY || aps->key_id == SS_SECUR_KEY_LOAD_KEY)) {
	}

	if (payload[0] != 0x05U || payload[1] != SS_STANDARD_NETWORK_KEY) {
		return false;
	}

	key_seq = payload[18];
	dst_ieee = &payload[19];
	src_ieee = &payload[27];
	if (!ZB_IS_64BIT_ADDR_ZERO(dst_ieee) &&
	    memcmp(dst_ieee, g_zbMacPib.extAddress, sizeof(addrExt_t)) != 0) {
		return false;
	}

	material = &ss_ib.nwkSecurMaterialSet[0];
	memcpy(material->key, &payload[2], SEC_KEY_LEN);
	material->keySeqNum = key_seq;
	material->keyType = SS_STANDARD_NETWORK_KEY;
	ss_ib.activeSecureMaterialIndex = 0U;
	ss_ib.activeKeySeqNum = key_seq;
	ss_ib.securityLevel = 5U;
	aps_ib.aps_authenticated = 1U;
	aps_ib.aps_use_insecure_join = FALSE;
	if (!ZB_IS_64BIT_ADDR_ZERO(src_ieee)) {
		ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, src_ieee);
	}
	/*
	 * Defer zb_info_save() ~15s instead of running it synchronously here.
	 * On the Zephyr NVS backend the save chain (nv_nwkFrameCount +
	 * zdo_ssInfoSaveToFlash + zb_info_save) holds arch_irq_lock through
	 * back-to-back flash page programs / sector erases, stalling the ZB
	 * thread long enough that the post-join Node_Desc_req /
	 * Active_EP_req / SimpleDesc_req exchange never gets serviced and
	 * the device falls into a watchdog reset loop (PC stuck at __reset,
	 * irq_en=0).  The persistence is only consulted on the next
	 * power-up, so the long delay is safe as long as the save lands
	 * before any reset.  Mirrors the same fix applied to
	 * nwk_ed_minimal_complete_join and bdb_mgmtPermitJoiningConfirm.
	 */
	(void)TL_ZB_TIMER_SCHEDULE(zb_minimal_transport_key_save_timer,
				   NULL, 15000U);
	LOG_INF("joined RX: installed nwk key seq=%u tc=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		key_seq, ss_ib.trust_center_address[0], ss_ib.trust_center_address[1],
		ss_ib.trust_center_address[2], ss_ib.trust_center_address[3],
		ss_ib.trust_center_address[4], ss_ib.trust_center_address[5],
		ss_ib.trust_center_address[6], ss_ib.trust_center_address[7]);
	tl_zbNwkEdMinimalTransportKeyDone();
/* returned from TransportKeyDone */
	return true;
}

static void zb_minimal_handle_joined_data_frame(u8 *psdu, u8 len)
{
	zb_minimal_mac_frame_t mac;
	zb_minimal_nwk_frame_t nwk;
	zb_minimal_aps_frame_t aps;
	u16 mac_frame_ctrl;

	if ((psdu == NULL) || (len < 2U)) {
		return;
	}

	mac_frame_ctrl = zb_u16_from_le(psdu);
	if ((mac_frame_ctrl & MAC_FCF_FRAME_TYPE_MASK) != MAC_FRAME_DATA) {
		return;
	}
	if (!zb_minimal_interview_frame_relevant(psdu, len)) {
		return;
	}

	if (!zb_minimal_parse_mac_frame(psdu, len, &mac)) {
		return;
	}

	if (!zb_minimal_parse_nwk_frame(mac.payload, mac.payload_len, &nwk)) {
		return;
	}

	if (nwk.security) {
		if (!zb_minimal_decrypt_nwk_payload((u8 *)mac.payload, mac.payload_len, &nwk)) {
			return;
		}
	}

	if (nwk.frame_type == FRAME_TYPE_COMMAND) {
		if ((nwk.payload_len >= 4U) && (nwk.payload[0] == NWK_CMD_REJOIN_RESPONSE)) {
			tl_zbNwkEdMinimalRejoinResponseReceived(nwk.payload[3],
								zb_u16_from_le(&nwk.payload[1]),
								nwk.src_addr);
			return;
		}
		if (zb_minimal_handle_nwk_leave_command(&nwk)) {
			return;
		}
		if ((nwk.payload_len >= 3U) && (nwk.payload[0] == 0x0CU)) {
			tl_zbNwkEdMinimalTimeoutRspReceived(nwk.payload[1], nwk.payload[2]);
		}
		return;
	}

	if (nwk.frame_type != FRAME_TYPE_DATA) {
		return;
	}

	if (!zb_minimal_parse_aps_frame(nwk.payload, nwk.payload_len, &aps)) {
		return;
	}

	if (aps.security) {
		if (!zb_minimal_decrypt_aps_payload((u8 *)nwk.payload, nwk.payload_len, &aps)) {
			return;
		}
	}

	if (aps.ack_req) {
		(void)zb_minimal_send_aps_ack(&nwk, &aps);
	}

	if (zb_minimal_handle_transport_key(&aps)) {
		return;
	}

	if (aps.profile_id == ZDO_PROFILE_ID && (aps.cluster_id & 0x8000U) != 0U) {
		tl_zbMinimalZdoResponseIndication(nwk.src_addr, aps.cluster_id, aps.payload,
						  aps.payload_len);
		return;
	}

	if (zb_minimal_handle_zdo_request(nwk.src_addr, &aps)) {
		LOG_INF("minimal ZDO rsp sent src=0x%04x cluster=0x%04x", nwk.src_addr,
			aps.cluster_id);
	} else if (aps.profile_id == ZDO_PROFILE_ID) {
	} else if (zb_minimal_handle_zcl_basic_read(nwk.src_addr, &aps)) {
	}
}

void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi)
{
	bool can_process = false;
	u16 mac_frame_ctrl = 0U;
	u8 frame_type = 0xffU;

	ARG_UNUSED(rxBuf);
	ARG_UNUSED(timestamp);

	zb_minimal_join_gate_trace[1]++;
	if ((data != NULL) && (len >= 2U)) {
		mac_frame_ctrl = zb_u16_from_le(data);
		frame_type = (u8)(mac_frame_ctrl & MAC_FCF_FRAME_TYPE_MASK);
	}
	zb_minimal_join_gate_trace[2] = ((u32)frame_type << 24) |
					 ((u32)len << 16) |
					 ((u32)ackPkt << 8) |
					 rf_getChannel();
	zb_minimal_join_gate_trace[5] = mac_frame_ctrl;
	zb_minimal_join_gate_trace[6] =
		((u32)((mac_frame_ctrl & MAC_FCF_FRAME_PENDING_MASK) != 0U) << 24) |
		((u32)((mac_frame_ctrl & MAC_FCF_ACK_REQ_BIT) != 0U) << 16) |
		((u32)(data != NULL ? data[2] : 0U) << 8) |
		frame_type;

	tl_zbNwkEdMinimalMacRxIndicate(data, len, rssi);
	can_process = (data != NULL) &&
		      (len >= MAC_MIN_HDR_LEN) &&
		      tl_zbNwkEdMinimalCanProcessDataFrames();
	zb_minimal_join_gate_trace[3] = ((u32)(g_zbNwkCtx.joined ? 1U : 0U) << 24) |
					 ((u32)(can_process ? 1U : 0U) << 16) |
					 ((u32)(u8)rssi << 8) |
					 frame_type;

	if (!can_process) {
		zb_minimal_join_gate_trace[4] = ((u32)(data == NULL ? 1U : 0U) << 24) |
						 ((u32)(len < MAC_MIN_HDR_LEN ? 1U : 0U) << 16) |
						 ((u32)(frame_type == MAC_FRAME_DATA ? 1U : 0U) << 8) |
						 (u32)(g_zbNwkCtx.joined ? 1U : 0U);
		return;
	}

	zb_minimal_join_gate_trace[4] = 0xd0010000U |
					 ((u32)(frame_type == MAC_FRAME_DATA ? 1U : 0U) << 8) |
					 rf_getChannel();
	zb_minimal_handle_joined_data_frame(data, len);
}

void zb_macDataSendHandler(void)
{
}

u8 tl_zbMacHdrSize(u16 frameCtrl)
{
	u8 size = MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN;
	u8 dst_mode = (u8)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	u8 src_mode = (u8)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	if (dst_mode != ZB_ADDR_NO_ADDR) {
		size += MAC_PAN_ID_FIELD_LEN;
		size += (dst_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	}

	if (src_mode != ZB_ADDR_NO_ADDR) {
		if ((frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
			size += MAC_PAN_ID_FIELD_LEN;
		}
		size += (src_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	}

	return size;
}

u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 send)
{
	ARG_UNUSED(addrMode);
	ARG_UNUSED(addr);
	ARG_UNUSED(send);

	return MAC_STA_NO_DATA;
}
