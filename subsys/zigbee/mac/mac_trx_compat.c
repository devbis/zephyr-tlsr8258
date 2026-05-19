/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/logging/log.h>

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

LOG_MODULE_REGISTER(zigbee_mac_trx_compat, CONFIG_ZIGBEE_LOG_LEVEL);

sys_diagnostics_t g_sysDiags;
zb_info_t g_zbInfo;

extern const u8 tcLinkKeyCentralDefault[];
extern u8 ss_keyHash(u8 *input, u8 *key, u8 *output);

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

#define ZB_MINIMAL_NWK_AUX_HDR_LEN 14U
#define ZB_MINIMAL_NWK_MIC_LEN     4U
#define ZB_MINIMAL_APS_MIC_LEN     4U
#define ZB_MINIMAL_CCM_L_VAL       2U

static u8 *zb_minimal_nwk_key_by_seq(u8 key_seq)
{
	for (u8 i = 0U; i < SECUR_N_SECUR_MATERIAL; i++) {
		if (ss_ib.nwkSecurMaterialSet[i].keySeqNum == key_seq) {
			return ss_ib.nwkSecurMaterialSet[i].key;
		}
	}

	if (ss_ib.activeSecureMaterialIndex < SECUR_N_SECUR_MATERIAL) {
		return ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
	}

	return NULL;
}

static void zb_minimal_aes_encrypt_block(const u8 *key, const u8 *in, u8 *out)
{
	drv_aes_encrypt((u8 *)key, (u8 *)in, out);
}

static void zb_minimal_ccm_xor_block(u8 *dst, const u8 *src)
{
	for (u8 i = 0U; i < 16U; i++) {
		dst[i] ^= src[i];
	}
}

static void zb_minimal_ccm_mac_block(const u8 *key, u8 state[16], const u8 block[16])
{
	u8 tmp[16];

	memcpy(tmp, state, sizeof(tmp));
	zb_minimal_ccm_xor_block(tmp, block);
	zb_minimal_aes_encrypt_block(key, tmp, state);
}

static void zb_minimal_ccm_ctr_block(const u8 *key, const u8 nonce[13], u16 counter, u8 stream[16])
{
	u8 a_i[16] = {0};

	a_i[0] = ZB_MINIMAL_CCM_L_VAL - 1U;
	memcpy(&a_i[1], nonce, 13U);
	a_i[14] = (u8)(counter >> 8);
	a_i[15] = (u8)counter;
	zb_minimal_aes_encrypt_block(key, a_i, stream);
}

static void zb_minimal_ccm_add_aad(const u8 *key, u8 state[16], const u8 *aad, u8 aad_len)
{
	u8 block[16] = {0};
	u8 idx = 0U;
	u8 off = 0U;

	if ((aad == NULL) || (aad_len == 0U)) {
		return;
	}

	block[idx++] = 0U;
	block[idx++] = aad_len;
	while ((off < aad_len) && (idx < sizeof(block))) {
		block[idx++] = aad[off++];
	}
	zb_minimal_ccm_mac_block(key, state, block);

	while (off < aad_len) {
		memset(block, 0, sizeof(block));
		for (idx = 0U; (idx < sizeof(block)) && (off < aad_len); idx++) {
			block[idx] = aad[off++];
		}
		zb_minimal_ccm_mac_block(key, state, block);
	}
}

static void zb_minimal_ccm_add_msg(const u8 *key, u8 state[16], const u8 *msg, u8 msg_len)
{
	u8 block[16] = {0};
	u8 off = 0U;
	u8 idx;

	while (off < msg_len) {
		memset(block, 0, sizeof(block));
		for (idx = 0U; (idx < sizeof(block)) && (off < msg_len); idx++) {
			block[idx] = msg[off++];
		}
		zb_minimal_ccm_mac_block(key, state, block);
	}
}

static bool zb_minimal_ccm_decrypt_auth(const u8 *key, const u8 nonce[13], u8 mic_len, u8 *cipher,
					u8 cipher_len, const u8 *aad, u8 aad_len, u8 *mic)
{
	u8 mac_state[16] = {0};
	u8 b0[16] = {0};
	u8 s0[16];
	u8 tag[16];
	u8 stream[16];
	u8 flags = (u8)(ZB_MINIMAL_CCM_L_VAL - 1U);
	u8 off = 0U;
	u16 counter = 1U;

	if ((key == NULL) || (nonce == NULL) || (cipher == NULL) || (mic == NULL) ||
	    (mic_len != ZB_MINIMAL_NWK_MIC_LEN)) {
		return false;
	}

	while (off < cipher_len) {
		u8 blk_len = MIN((u8)16U, (u8)(cipher_len - off));

		zb_minimal_ccm_ctr_block(key, nonce, counter++, stream);
		for (u8 i = 0U; i < blk_len; i++) {
			cipher[off + i] ^= stream[i];
		}
		off += blk_len;
	}

	if (aad_len != 0U) {
		flags |= 0x40U;
	}
	flags |= (u8)(((mic_len - 2U) / 2U) << 3);
	b0[0] = flags;
	memcpy(&b0[1], nonce, 13U);
	b0[14] = (u8)(cipher_len >> 8);
	b0[15] = cipher_len;

	zb_minimal_ccm_mac_block(key, mac_state, b0);
	zb_minimal_ccm_add_aad(key, mac_state, aad, aad_len);
	zb_minimal_ccm_add_msg(key, mac_state, cipher, cipher_len);

	zb_minimal_ccm_ctr_block(key, nonce, 0U, s0);
	for (u8 i = 0U; i < mic_len; i++) {
		tag[i] = mac_state[i] ^ s0[i];
	}

	return memcmp(tag, mic, mic_len) == 0;
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

	if (g_zbNwkCtx.joined) {
		return true;
	}

	if (!zb_minimal_parse_mac_addr_info(psdu, len, &info)) {
		return false;
	}

	if (info.dst_short_valid) {
		dst_match = (info.dst_short_addr == g_zbMacPib.shortAddress) ||
			    (info.dst_short_addr == MAC_SHORT_ADDR_BROADCAST);
	} else if (info.dst_ext_valid) {
		dst_match = memcmp(info.dst_ext_addr, g_zbMacPib.extAddress, sizeof(addrExt_t)) == 0;
	}

	if (!dst_match) {
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
		src_match = true;
	}

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
			memcpy(frame->security_src_ext, g_zbMacPib.coordExtAddress,
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
	u8 sec_ctrl_idx;
	bool transport_key_sec = false;
	bool auth_ok;

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
	cipher_len = (u8)(frame->payload_len - frame->mic_len);
	mic = &cipher[cipher_len];

	if (cipher_len > sizeof(cipher_shadow)) {
		LOG_DBG("joined RX: APS cipher too large (%u)", cipher_len);
		return false;
	}

	memcpy(cipher_shadow, cipher, cipher_len);
	auth_ok = zb_minimal_ccm_decrypt_auth(key, nonce, frame->mic_len, cipher_shadow, cipher_len,
						 aad, (u8)(frame->payload - aps_psdu), mic);
	if (auth_ok && transport_key_sec) {
	}
	if (!auth_ok && transport_key_sec && key != NULL &&
	    memcmp(key, tcLinkKeyCentralDefault, SEC_KEY_LEN) == 0) {
		u8 hash_input = 0U;

		(void)ss_keyHash(&hash_input, key, hashed_key);
		memcpy(cipher_shadow, cipher, cipher_len);
		auth_ok = zb_minimal_ccm_decrypt_auth(hashed_key, nonce, frame->mic_len,
						      cipher_shadow, cipher_len, aad,
						      (u8)(frame->payload - aps_psdu), mic);
		if (auth_ok) {
			LOG_INF("joined RX: APS decrypt used hashed TC link key");
		}
	}

	if (!auth_ok) {
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
	dst.txOptions = 0U;
	dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst.dstAddr.shortAddr = dst_nwk_addr;
	return af_dataSend(ZDO_EP, &dst, cluster_id, payload_len, (u8 *)payload, &aps_cnt);
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

	switch (aps->cluster_id) {
	case NODE_DESC_REQ_CLID: {
		zdo_node_descript_resp_t rsp;

		if (nwk_addr_interest != g_zbNIB.nwkAddr) {
			return false;
		}

		memset(&rsp, 0, sizeof(rsp));
		rsp.seq_num = seq;
		rsp.status = ZDO_SUCCESS;
		rsp.nwk_addr_interest = g_zbNIB.nwkAddr;
		af_nodeDescriptorCopy(&rsp.node_descriptor);
		return zb_minimal_send_zdo_response(src_nwk_addr, NODE_DESC_RSP_CLID,
						      (const u8 *)&rsp, sizeof(rsp)) == APS_STATUS_SUCCESS;
	}
	case POWER_DESC_REQ_CLID: {
		zdo_power_descriptor_resp_t rsp;

		if (nwk_addr_interest != g_zbNIB.nwkAddr) {
			return false;
		}

		memset(&rsp, 0, sizeof(rsp));
		rsp.seq_num = seq;
		rsp.status = ZDO_SUCCESS;
		rsp.nwk_addr_interest = g_zbNIB.nwkAddr;
		af_powerDescriptorCopy(&rsp.power_descriptor);
		return zb_minimal_send_zdo_response(src_nwk_addr, POWER_DESC_RSP_CLID,
						      (const u8 *)&rsp, sizeof(rsp)) == APS_STATUS_SUCCESS;
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
		return zb_minimal_send_zdo_response(src_nwk_addr, ACTIVE_EP_RSP_CLID, payload,
						      (u16)(5U + MIN(ep_count, MAX_REQUESTED_CLUSTER_NUMBER))) ==
		       APS_STATUS_SUCCESS;
	}
	case SIMPLE_DESC_REQ_CLID: {
		af_simple_descriptor_t *sd;
		u8 simple_len;

		if (aps->payload_len < 4U || nwk_addr_interest != g_zbNIB.nwkAddr) {
			return false;
		}

		memset(payload, 0, sizeof(payload));
		payload[0] = seq;
		payload[1] = ZDO_SUCCESS;
		COPY_U16TOBUFFER(&payload[2], g_zbNIB.nwkAddr);

		sd = af_simpleDescGet(aps->payload[3]);
		if (sd == NULL) {
			payload[1] = ZDO_INVALID_EP;
			payload[4] = 0U;
			return zb_minimal_send_zdo_response(src_nwk_addr, SIMPLE_DESC_RSP_CLID,
							      payload, 5U) == APS_STATUS_SUCCESS;
		}

		simple_len = af_simpleDescriptorCopy(&payload[5], sd);
		payload[4] = simple_len;
		return zb_minimal_send_zdo_response(src_nwk_addr, SIMPLE_DESC_RSP_CLID, payload,
						      (u16)(5U + simple_len)) == APS_STATUS_SUCCESS;
	}
	default:
		return false;
	}
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
	zb_info_save(NULL);
	LOG_INF("joined RX: installed nwk key seq=%u tc=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
		key_seq, ss_ib.trust_center_address[0], ss_ib.trust_center_address[1],
		ss_ib.trust_center_address[2], ss_ib.trust_center_address[3],
		ss_ib.trust_center_address[4], ss_ib.trust_center_address[5],
		ss_ib.trust_center_address[6], ss_ib.trust_center_address[7]);
	tl_zbNwkEdMinimalTransportKeyDone();
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

	if (nwk.frame_type != FRAME_TYPE_DATA) {
		return;
	}

	if (nwk.security) {
		if (!zb_minimal_decrypt_nwk_payload((u8 *)mac.payload, mac.payload_len, &nwk)) {
			return;
		}
	}

	if (!zb_minimal_parse_aps_frame(nwk.payload, nwk.payload_len, &aps)) {
		return;
	}

	if (aps.security) {
		if (!zb_minimal_decrypt_aps_payload((u8 *)nwk.payload, nwk.payload_len, &aps)) {
			return;
		}
	}

	if (zb_minimal_handle_transport_key(&aps)) {
		return;
	}

	if (zb_minimal_handle_zdo_request(nwk.src_addr, &aps)) {
		LOG_INF("minimal ZDO rsp sent src=0x%04x cluster=0x%04x", nwk.src_addr,
			aps.cluster_id);
	}
}

void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi)
{
	ARG_UNUSED(rxBuf);
	ARG_UNUSED(ackPkt);
	ARG_UNUSED(timestamp);
	ARG_UNUSED(rssi);

	tl_zbNwkEdMinimalMacRxIndicate(data, len, rssi);

	if ((data == NULL) || (len < MAC_MIN_HDR_LEN) || !tl_zbNwkEdMinimalCanProcessDataFrames()) {
		return;
	}

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
