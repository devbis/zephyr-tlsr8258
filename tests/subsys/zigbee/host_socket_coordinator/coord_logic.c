/* SPDX-License-Identifier: Apache-2.0 */

#include "coord_logic.h"
#include "coord_ccm.h"

#include <string.h>

#define ZB_COORD_SHORT_ADDR 0x0000u
#define ZB_NO_SHORT_ADDR    0xffffu
#define ZB_DEVICE_IEEE      0xa4c138e050020002ULL
#define ZB_COORD_IEEE       0x00124b0000000001ULL
#define ZB_PROFILE_HA       0x0104u
#define ZB_PAN_ID           0x5b27u
#define ZB_ADDR_MODE_SHORT  0x02u
#define ZB_ADDR_MODE_EXT    0x03u
#define ZB_STANDARD_NWK_KEY 0x01u

#define ZB_MAC_FCF_BEACON_SHORT  0x8000u
#define ZB_MAC_FCF_COMMAND_SHORT 0x8863u
#define ZB_MAC_FCF_DATA_EXT_SHORT 0x8c61u
#define ZB_MAC_CMD_BEACON_REQ    0x07u
#define ZB_MAC_CMD_ASSOC_REQ     0x01u
#define ZB_MAC_CMD_ASSOC_RSP     0x02u
#define ZB_MAC_CMD_DATA_REQ      0x04u

#define ZB_ZDO_DEVICE_ANNCE    0x0013u
#define ZB_ZDO_NODE_DESC_REQ   0x0002u
#define ZB_ZDO_NODE_DESC_RSP   0x8002u
#define ZB_ZDO_ACTIVE_EP_REQ   0x0005u
#define ZB_ZDO_ACTIVE_EP_RSP   0x8005u
#define ZB_ZDO_SIMPLE_DESC_REQ 0x0004u
#define ZB_ZDO_SIMPLE_DESC_RSP 0x8004u
#define ZB_CLUSTER_BASIC       0x0000u
#define ZB_CLUSTER_NWK_MGMT    0xff00u
#define ZB_NWK_TIMEOUT_REQ     0x0bu
#define ZB_NWK_TIMEOUT_RSP     0x0cu
#define ZB_ZCL_CMD_READ        0x00u
#define ZB_ZCL_CMD_READ_RSP    0x01u
#define ZB_ZCL_ATTR_MODEL_ID   0x0005u
#define ZB_ZCL_TYPE_CHAR_STR   0x42u

static void put_le16(uint8_t *buf, uint16_t value)
{
	buf[0] = (uint8_t)value;
	buf[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *buf)
{
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static bool parse_mac_command(const uint8_t *psdu, size_t psdu_len,
			      uint16_t *fcf_out, size_t *cmd_idx_out)
{
	uint16_t fcf;
	uint8_t dst_mode;
	uint8_t src_mode;
	bool intra_pan;
	size_t idx = 3U;

	if (psdu == NULL || psdu_len < 4U) {
		return false;
	}

	fcf = get_le16(&psdu[0]);
	if ((fcf & 0x0007U) != 0x0003U) {
		return false;
	}

	dst_mode = (uint8_t)((fcf >> 10) & 0x03U);
	src_mode = (uint8_t)((fcf >> 14) & 0x03U);
	intra_pan = (fcf & 0x0040U) != 0U;

	if (dst_mode != 0U) {
		idx += 2U;
		if (dst_mode == ZB_ADDR_MODE_SHORT) {
			idx += 2U;
		} else if (dst_mode == ZB_ADDR_MODE_EXT) {
			idx += 8U;
		} else {
			return false;
		}
	}

	if (src_mode != 0U) {
		if (!intra_pan) {
			idx += 2U;
		}
		if (src_mode == ZB_ADDR_MODE_SHORT) {
			idx += 2U;
		} else if (src_mode == ZB_ADDR_MODE_EXT) {
			idx += 8U;
		} else {
			return false;
		}
	}

	if (idx >= psdu_len) {
		return false;
	}

	if (fcf_out != NULL) {
		*fcf_out = fcf;
	}
	if (cmd_idx_out != NULL) {
		*cmd_idx_out = idx;
	}

	return true;
}

static size_t mac_header_len_from_fcf(const uint8_t *psdu, size_t psdu_len, uint16_t *fcf_out)
{
	uint16_t fcf;
	uint8_t dst_mode;
	uint8_t src_mode;
	bool intra_pan;
	size_t idx = 3U;

	if (psdu == NULL || psdu_len < idx) {
		return 0U;
	}

	fcf = get_le16(&psdu[0]);
	dst_mode = (uint8_t)((fcf >> 10) & 0x03U);
	src_mode = (uint8_t)((fcf >> 14) & 0x03U);
	intra_pan = (fcf & 0x0040U) != 0U;

	if (dst_mode != 0U) {
		idx += 2U;
		idx += (dst_mode == ZB_ADDR_MODE_SHORT) ? 2U :
		       (dst_mode == ZB_ADDR_MODE_EXT) ? 8U : 0U;
		if ((dst_mode != ZB_ADDR_MODE_SHORT) && (dst_mode != ZB_ADDR_MODE_EXT)) {
			return 0U;
		}
	}

	if (src_mode != 0U) {
		if (!intra_pan) {
			idx += 2U;
		}
		idx += (src_mode == ZB_ADDR_MODE_SHORT) ? 2U :
		       (src_mode == ZB_ADDR_MODE_EXT) ? 8U : 0U;
		if ((src_mode != ZB_ADDR_MODE_SHORT) && (src_mode != ZB_ADDR_MODE_EXT)) {
			return 0U;
		}
	}

	if (idx > psdu_len) {
		return 0U;
	}

	if (fcf_out != NULL) {
		*fcf_out = fcf;
	}

	return idx;
}

static bool identify_real_transport_key(const uint8_t *psdu, size_t psdu_len)
{
	const uint8_t *nwk;
	const uint8_t *aps;
	const uint8_t *payload;
	uint16_t mac_fcf;
	uint16_t nwk_fcf;
	size_t mac_hdr_len;

	mac_hdr_len = mac_header_len_from_fcf(psdu, psdu_len, &mac_fcf);
	if (mac_hdr_len == 0U || (mac_fcf & 0x0007U) != 0x0001U ||
	    psdu_len < (mac_hdr_len + 10U)) {
		return false;
	}

	nwk = &psdu[mac_hdr_len];
	nwk_fcf = get_le16(nwk);
	if ((nwk_fcf & 0x0003U) != 0x0000U) {
		return false;
	}

	aps = &nwk[8];
	if ((aps[0] & 0x03U) != 0x01U || psdu_len < (mac_hdr_len + 12U)) {
		return false;
	}

	payload = &aps[2];
	return payload[0] == 0x05U && payload[1] == ZB_STANDARD_NWK_KEY;
}

static enum zb_host_socket_frame_type identify_decrypted_aps_frame(const uint8_t *aps,
								   size_t aps_len)
{
	uint16_t cluster;
	uint16_t profile;
	uint8_t command;
	size_t payload_len;

	if (aps == NULL || aps_len < 8U) {
		return ZB_HOST_SOCKET_FRAME_DATA_REQ;
	}

	cluster = get_le16(&aps[2]);
	profile = get_le16(&aps[4]);
	command = (aps_len > 8U) ? aps[8] : 0U;
	payload_len = aps_len - 8U;

	switch (cluster) {
	case ZB_CLUSTER_NWK_MGMT:
		return (command == ZB_NWK_TIMEOUT_REQ) ?
			ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ :
			ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP;
	case ZB_ZDO_DEVICE_ANNCE:
		return (profile == 0x0000U) ? ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE :
					      ZB_HOST_SOCKET_FRAME_DATA_REQ;
	case ZB_ZDO_NODE_DESC_REQ:
		return (profile == 0x0000U) ? ZB_HOST_SOCKET_FRAME_NODE_DESC_REQ :
					      ZB_HOST_SOCKET_FRAME_DATA_REQ;
	case ZB_ZDO_NODE_DESC_RSP:
		return (profile == 0x0000U) ? ZB_HOST_SOCKET_FRAME_NODE_DESC_RSP :
					      ZB_HOST_SOCKET_FRAME_DATA_REQ;
	case ZB_ZDO_ACTIVE_EP_REQ:
		return (profile == 0x0000U) ? ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ :
					      ZB_HOST_SOCKET_FRAME_DATA_REQ;
	case ZB_ZDO_ACTIVE_EP_RSP:
		return (profile == 0x0000U) ? ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP :
					      ZB_HOST_SOCKET_FRAME_DATA_REQ;
	case ZB_ZDO_SIMPLE_DESC_REQ:
		return (profile == 0x0000U) ? ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ :
					      ZB_HOST_SOCKET_FRAME_DATA_REQ;
	case ZB_ZDO_SIMPLE_DESC_RSP:
		return (profile == 0x0000U) ? ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP :
					      ZB_HOST_SOCKET_FRAME_DATA_REQ;
	case ZB_CLUSTER_BASIC:
		if (command == ZB_ZCL_CMD_READ && payload_len >= 3U) {
			return ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ;
		}
		return ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP;
	default:
		return ZB_HOST_SOCKET_FRAME_DATA_REQ;
	}
}

static void put_le64(uint8_t *buf, uint64_t value)
{
	for (size_t i = 0; i < 8; i++) {
		buf[i] = (uint8_t)(value >> (8 * i));
	}
}

/* NWK key the daemon ships in its Transport-Key (encode_transport_key_frame). */
static const uint8_t ZB_NWK_KEY[16] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
};
#define ZB_NWK_AUX_LEN 14U

/*
 * Decrypt an inbound real NWK-secured DATA frame (MAC hdr + NWK hdr + NWK aux +
 * ciphertext + 4-byte MIC). On success the recovered APS frame (APS hdr + ASDU)
 * is written to `out` and *out_len set. The device clears the security level in
 * the on-air aux secCtrl byte but uses level 5 for the nonce/AAD (see
 * ss_nwkEnDecrypt.c), so we restore it. Returns true iff the MIC verifies.
 */
/*
 * Attempt to NWK-decrypt the frame, treating `nwk_off` as the offset of the
 * NWK header within the PSDU. Returns true (and fills out/out_len) only if the
 * frame is a secured NWK DATA frame whose CCM* MIC verifies.
 *
 * The device puts its NWK *data* frames (Device Announce, Link Status, ...)
 * on the native_sim medium NWK-only — no MAC header, NWK FCF at offset 0.
 * Its MAC *commands* (assoc-req, ...) are MAC-wrapped. So we try offset 0
 * first and fall back to the MAC-header offset; whichever MIC-verifies wins.
 */
static bool try_nwk_decrypt_best_effort_at(const uint8_t *psdu, size_t psdu_len, size_t nwk_off,
					   uint8_t *out, size_t *out_len, bool *mic_ok)
{
	uint16_t nwk_fcf;
	size_t nwk_hdr = 8U;

	if (psdu_len < nwk_off + 8U) {
		return false;
	}

	const uint8_t *nwk = &psdu[nwk_off];

	nwk_fcf = get_le16(nwk);
	if ((nwk_fcf & 0x0003U) != 0x0000U) {
		return false; /* not a NWK DATA frame */
	}
	if ((nwk_fcf & 0x0200U) == 0U) {
		return false; /* NWK security bit clear */
	}
	if (nwk_fcf & 0x0800U) { /* dst IEEE present */
		nwk_hdr += 8U;
	}
	if (nwk_fcf & 0x1000U) { /* src IEEE present */
		nwk_hdr += 8U;
	}

	if (psdu_len < nwk_off + nwk_hdr + ZB_NWK_AUX_LEN + 4U) {
		return false;
	}

	const uint8_t *aux = &nwk[nwk_hdr];
	uint8_t sec_ctrl = aux[0];
	uint8_t sec_ctrl5 = (uint8_t)((sec_ctrl & 0xf8U) | 0x05U); /* restore level 5 */

	uint8_t nonce[13];

	memcpy(&nonce[0], &aux[5], 8U);  /* srcExtAddr */
	memcpy(&nonce[8], &aux[1], 4U);  /* frameCounter (LE) */
	nonce[12] = sec_ctrl5;

	uint8_t aad[80];
	size_t aad_len = nwk_hdr + ZB_NWK_AUX_LEN;

	if (aad_len > sizeof(aad)) {
		return false;
	}
	memcpy(aad, nwk, aad_len);
	aad[nwk_hdr] = sec_ctrl5; /* secCtrl in AAD also uses level 5 */

	const uint8_t *cipher = &aux[ZB_NWK_AUX_LEN];
	size_t after = psdu_len - (nwk_off + nwk_hdr + ZB_NWK_AUX_LEN);
	size_t cipher_len = after - 4U;
	const uint8_t *mic = &cipher[cipher_len];

	if (cipher_len > 100U) {
		return false;
	}
	memcpy(out, cipher, cipher_len);
	if (mic_ok != NULL) {
		*mic_ok = coord_ccm_decrypt(ZB_NWK_KEY, nonce, out, (uint8_t)cipher_len,
					    aad, (uint8_t)aad_len, mic);
	} else {
		(void)coord_ccm_decrypt(ZB_NWK_KEY, nonce, out, (uint8_t)cipher_len,
					aad, (uint8_t)aad_len, mic);
	}
	*out_len = cipher_len;
	return true;
}

bool zb_host_socket_coord_nwk_decrypt(const uint8_t *psdu, size_t psdu_len,
				      uint8_t *out, size_t *out_len)
{
	bool mic_ok = false;

	if (psdu == NULL || psdu_len < 8U) {
		return false;
	}

	/* Device NWK data frames are NWK-only (no MAC header). */
	if (try_nwk_decrypt_best_effort_at(psdu, psdu_len, 0U, out, out_len, &mic_ok)) {
		return mic_ok;
	}

	/* Fall back: MAC-wrapped NWK data frame. */
	uint16_t mac_fcf;
	size_t mac_hdr = mac_header_len_from_fcf(psdu, psdu_len, &mac_fcf);

	if (mac_hdr != 0U && (mac_fcf & 0x0007U) == 0x0001U) {
		if (try_nwk_decrypt_best_effort_at(psdu, psdu_len, mac_hdr,
						   out, out_len, &mic_ok)) {
			return mic_ok;
		}
	}
	return false;
}

static enum zb_host_socket_frame_type identify_best_effort_nwk_frame(const uint8_t *psdu,
								     size_t psdu_len)
{
	uint8_t aps[128];
	size_t aps_len = 0U;
	bool mic_ok = false;
	uint16_t mac_fcf = 0U;
	size_t mac_hdr;
	enum zb_host_socket_frame_type type;

	if (try_nwk_decrypt_best_effort_at(psdu, psdu_len, 0U, aps, &aps_len, &mic_ok)) {
		type = identify_decrypted_aps_frame(aps, aps_len);
		if (type != ZB_HOST_SOCKET_FRAME_DATA_REQ) {
			return type;
		}
	}

	mac_hdr = mac_header_len_from_fcf(psdu, psdu_len, &mac_fcf);
	if (mac_hdr == 0U || (mac_fcf & 0x0007U) != 0x0001U) {
		return ZB_HOST_SOCKET_FRAME_DATA_REQ;
	}

	if (try_nwk_decrypt_best_effort_at(psdu, psdu_len, mac_hdr, aps, &aps_len, &mic_ok)) {
		type = identify_decrypted_aps_frame(aps, aps_len);
		if (type != ZB_HOST_SOCKET_FRAME_DATA_REQ) {
			return type;
		}
	}

	return ZB_HOST_SOCKET_FRAME_DATA_REQ;
}

static void queue_frame(struct zb_host_socket_coord *coord, enum zb_host_socket_frame_type type)
{
	if (coord->queued_count < sizeof(coord->queued_frames)) {
		coord->queued_frames[coord->queued_count++] = (uint8_t)type;
	}
}

static bool pop_frame(struct zb_host_socket_coord *coord, enum zb_host_socket_frame_type *type)
{
	if (coord->queued_count == 0U) {
		return false;
	}

	*type = (enum zb_host_socket_frame_type)coord->queued_frames[0];
	memmove(&coord->queued_frames[0], &coord->queued_frames[1], coord->queued_count - 1U);
	coord->queued_count--;
	return true;
}

static size_t encode_mac_command(uint8_t *wire, uint8_t seq, uint16_t dst, uint16_t src, uint8_t cmd)
{
	put_le16(&wire[0], ZB_MAC_FCF_COMMAND_SHORT);
	wire[2] = seq;
	put_le16(&wire[3], ZB_PAN_ID);
	put_le16(&wire[5], dst);
	put_le16(&wire[7], src);
	wire[9] = cmd;
	return 10U;
}

static size_t encode_beacon_frame(uint8_t *wire, uint8_t seq, uint16_t src)
{
	put_le16(&wire[0], ZB_MAC_FCF_BEACON_SHORT);
	wire[2] = seq;
	put_le16(&wire[3], ZB_PAN_ID);
	put_le16(&wire[5], src);
	wire[7] = 0xffU;
	wire[8] = 0xcfU;
	wire[9] = 0x00U;
	wire[10] = 0x00U;
	wire[11] = 0x00U;
	wire[12] = 0x22U;
	wire[13] = 0x84U;
	wire[14] = 0x70U;
	wire[15] = 0xeeU;
	wire[16] = 0x8fU;
	wire[17] = 0x4fU;
	wire[18] = 0x06U;
	wire[19] = 0x9dU;
	wire[20] = 0x09U;
	wire[21] = 0x3bU;
	wire[22] = 0xffU;
	wire[23] = 0xffU;
	wire[24] = 0xffU;
	wire[25] = 0x00U;
	return 26U;
}

static size_t encode_data_frame(uint8_t *wire, uint8_t seq, uint16_t dst, uint16_t src,
				uint16_t cluster, uint8_t command,
				const uint8_t *payload, size_t payload_len)
{
	size_t idx = 15U;

	put_le16(&wire[0], 0x8861u);
	wire[2] = seq;
	put_le16(&wire[3], ZB_PAN_ID);
	put_le16(&wire[5], dst);
	put_le16(&wire[7], src);
	put_le16(&wire[9], ZB_PROFILE_HA);
	put_le16(&wire[11], cluster);
	wire[13] = 1U;
	wire[14] = 1U;
	wire[idx++] = 0x00U;
	wire[idx++] = seq;
	wire[idx++] = command;
	if (payload_len != 0U) {
		memcpy(&wire[idx], payload, payload_len);
		idx += payload_len;
	}

	return idx;
}

/*
 * ZDO request framing. Identical layout to encode_data_frame() but with the
 * ZDO profile (0x0000) and ZDO endpoint (0) instead of the HA defaults — the
 * device's af_aps_data_entry only routes a frame to the ZDP handlers when
 * profile_id==ZDO_PROFILE_ID && dst_ep==ZDO_EP, so an HA-profiled/EP1 "ZDO"
 * request is silently dropped and never answered. The `command` byte doubles
 * as the ZDP transaction sequence number (payload[0] at the handler), so pass
 * 0x00 and let the real ZDP params follow.
 */
static size_t encode_zdo_frame(uint8_t *wire, uint8_t seq, uint16_t dst, uint16_t src,
			       uint16_t cluster, const uint8_t *payload, size_t payload_len)
{
	size_t len = encode_data_frame(wire, seq, dst, src, cluster, 0x00U, payload, payload_len);

	put_le16(&wire[9], 0x0000u); /* ZDO_PROFILE_ID */
	wire[13] = 0U;               /* dst endpoint = ZDO_EP */
	wire[14] = 0U;               /* src endpoint = ZDO_EP */
	return len;
}

static size_t encode_transport_key_frame(uint8_t *wire, uint8_t seq, uint16_t dst_short,
					 uint16_t src_short)
{
	put_le16(&wire[0], ZB_MAC_FCF_DATA_EXT_SHORT);
	wire[2] = seq;
	put_le16(&wire[3], ZB_PAN_ID);
	put_le64(&wire[5], ZB_DEVICE_IEEE);
	put_le16(&wire[13], src_short);
	put_le16(&wire[15], 0x0008u);
	put_le16(&wire[17], dst_short);
	put_le16(&wire[19], src_short);
	wire[21] = 30U;
	wire[22] = 1U;
	wire[23] = 0x01U;
	wire[24] = 1U;
	wire[25] = 0x05U;
	wire[26] = ZB_STANDARD_NWK_KEY;
	for (size_t i = 0U; i < 16U; i++) {
		wire[27U + i] = (uint8_t)(i + 1U);
	}
	wire[43] = 1U;
	put_le64(&wire[44], ZB_DEVICE_IEEE);
	put_le64(&wire[52], ZB_COORD_IEEE);
	return 60U;
}

enum zb_host_socket_frame_type zb_host_socket_coord_identify_frame(const uint8_t *psdu, size_t psdu_len)
{
	uint16_t fcf;
	uint16_t cluster;
	uint8_t command;
	size_t cmd_idx;
	size_t payload_len;

	if (psdu_len >= 3U && get_le16(&psdu[0]) == ZB_MAC_FCF_BEACON_SHORT) {
		return ZB_HOST_SOCKET_FRAME_BEACON;
	}

	if (parse_mac_command(psdu, psdu_len, &fcf, &cmd_idx)) {
		switch (psdu[cmd_idx]) {
		case ZB_MAC_CMD_BEACON_REQ:
			return ZB_HOST_SOCKET_FRAME_BEACON_REQ;
		case ZB_MAC_CMD_ASSOC_REQ:
			return ZB_HOST_SOCKET_FRAME_ASSOC_REQ;
		case ZB_MAC_CMD_ASSOC_RSP:
			return ZB_HOST_SOCKET_FRAME_ASSOC_RSP;
		case ZB_MAC_CMD_DATA_REQ:
			return ZB_HOST_SOCKET_FRAME_DATA_REQ;
		default:
			break;
		}
	}

	if (identify_real_transport_key(psdu, psdu_len)) {
		return ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY;
	}

	{
		enum zb_host_socket_frame_type type = identify_best_effort_nwk_frame(psdu, psdu_len);

		if (type != ZB_HOST_SOCKET_FRAME_DATA_REQ) {
			return type;
		}
	}

	if (psdu_len < 18U || get_le16(&psdu[0]) != 0x8861u) {
		return ZB_HOST_SOCKET_FRAME_DATA_REQ;
	}

	cluster = get_le16(&psdu[11]);
	command = psdu[17];
	payload_len = psdu_len - 18U;

	switch (cluster) {
	case 0x0038u:
		return ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY;
	case ZB_CLUSTER_NWK_MGMT:
		return (command == ZB_NWK_TIMEOUT_REQ) ?
			ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ :
			ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP;
	case ZB_ZDO_DEVICE_ANNCE:
		return ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE;
	case ZB_ZDO_NODE_DESC_REQ:
		return ZB_HOST_SOCKET_FRAME_NODE_DESC_REQ;
	case ZB_ZDO_NODE_DESC_RSP:
		return ZB_HOST_SOCKET_FRAME_NODE_DESC_RSP;
	case ZB_ZDO_ACTIVE_EP_REQ:
		return ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ;
	case ZB_ZDO_ACTIVE_EP_RSP:
		return ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP;
	case ZB_ZDO_SIMPLE_DESC_REQ:
		return ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ;
	case ZB_ZDO_SIMPLE_DESC_RSP:
		return ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP;
	case ZB_CLUSTER_BASIC:
		if (command == ZB_ZCL_CMD_READ && payload_len >= 2U) {
			return ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ;
		}
		return ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP;
	default:
		return ZB_HOST_SOCKET_FRAME_DATA_REQ;
	}
}

static size_t encode_frame(enum zb_host_socket_frame_type type, uint8_t *wire,
			   uint16_t src, uint16_t dst, const char *model_id)
{
	uint8_t payload[48];
	size_t len = 0U;

	switch (type) {
	case ZB_HOST_SOCKET_FRAME_BEACON:
		return encode_beacon_frame(wire, 1U, src);
	case ZB_HOST_SOCKET_FRAME_ASSOC_RSP:
		len = encode_mac_command(wire, 1U, ZB_NO_SHORT_ADDR, src, ZB_MAC_CMD_ASSOC_RSP);
		put_le16(&wire[10], dst);
		wire[12] = 0U;
		return len + 3U;
	case ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY:
		return encode_transport_key_frame(wire, 2U, dst, src);
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP:
		payload[0] = ZB_NWK_TIMEOUT_RSP;
		payload[1] = 0x00U;
		payload[2] = 0x02U;
		return encode_data_frame(wire, 3U, dst, src, ZB_CLUSTER_NWK_MGMT,
					 ZB_NWK_TIMEOUT_RSP, payload, 3U);
	case ZB_HOST_SOCKET_FRAME_NODE_DESC_REQ:
		put_le16(&payload[0], dst);
		return encode_zdo_frame(wire, 4U, dst, src, ZB_ZDO_NODE_DESC_REQ,
					payload, 2U);
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ:
		put_le16(&payload[0], dst);
		return encode_zdo_frame(wire, 4U, dst, src, ZB_ZDO_ACTIVE_EP_REQ,
					payload, 2U);
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ:
		put_le16(&payload[0], dst);
		payload[2] = 1U;
		return encode_zdo_frame(wire, 5U, dst, src, ZB_ZDO_SIMPLE_DESC_REQ,
					payload, 3U);
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ:
		put_le16(&payload[0], ZB_ZCL_ATTR_MODEL_ID);
		return encode_data_frame(wire, 6U, dst, src, ZB_CLUSTER_BASIC, ZB_ZCL_CMD_READ,
					 payload, 2U);
	case ZB_HOST_SOCKET_FRAME_ASSOC_REQ:
		len = encode_mac_command(wire, 1U, dst, src, ZB_MAC_CMD_ASSOC_REQ);
		put_le64(&wire[10], ZB_DEVICE_IEEE);
		return len + 8U;
	case ZB_HOST_SOCKET_FRAME_DATA_REQ:
		return encode_mac_command(wire, 2U, dst, src, ZB_MAC_CMD_DATA_REQ);
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ:
		payload[0] = ZB_NWK_TIMEOUT_REQ;
		payload[1] = 0x08U;
		return encode_data_frame(wire, 3U, dst, src, ZB_CLUSTER_NWK_MGMT,
					 ZB_NWK_TIMEOUT_REQ, payload, 2U);
	case ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE:
		put_le16(&payload[0], src);
		put_le64(&payload[2], ZB_DEVICE_IEEE);
		payload[10] = 0x80U;
		return encode_data_frame(wire, 4U, dst, src, ZB_ZDO_DEVICE_ANNCE, 0x00U,
					 payload, 11U);
	case ZB_HOST_SOCKET_FRAME_NODE_DESC_RSP:
		/* Simulated device reply (used by the unit test's make_tx). The
		 * daemon only keys identification on the cluster (0x8002); the
		 * 13-byte node descriptor body is zero-filled for the test. */
		payload[0] = 0x00U;          /* status: SUCCESS */
		put_le16(&payload[1], src);  /* NWKAddrOfInterest */
		memset(&payload[3], 0, 13U); /* node descriptor */
		return encode_data_frame(wire, 4U, dst, src, ZB_ZDO_NODE_DESC_RSP, 0x00U,
					 payload, 16U);
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP:
		payload[0] = 0x00U;
		put_le16(&payload[1], src);
		payload[3] = 1U;
		payload[4] = 1U;
		return encode_data_frame(wire, 5U, dst, src, ZB_ZDO_ACTIVE_EP_RSP, 0x00U,
					 payload, 5U);
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP:
		payload[0] = 0x00U;
		put_le16(&payload[1], src);
		payload[3] = 8U;
		payload[4] = 1U;
		put_le16(&payload[5], ZB_PROFILE_HA);
		put_le16(&payload[7], 0x0100U);
		payload[9] = 0U;
		payload[10] = 1U;
		put_le16(&payload[11], ZB_CLUSTER_BASIC);
		return encode_data_frame(wire, 6U, dst, src, ZB_ZDO_SIMPLE_DESC_RSP, 0x00U,
					 payload, 13U);
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP: {
		size_t model_len = strlen(model_id != NULL ? model_id : "");

		put_le16(&payload[0], ZB_ZCL_ATTR_MODEL_ID);
		payload[2] = 0x00U;
		payload[3] = ZB_ZCL_TYPE_CHAR_STR;
		payload[4] = (uint8_t)model_len;
		memcpy(&payload[5], model_id, model_len);
		return encode_data_frame(wire, 7U, dst, src, ZB_CLUSTER_BASIC,
					 ZB_ZCL_CMD_READ_RSP, payload, model_len + 5U);
	}
	default:
		break;
	}

	return 0U;
}

struct zb_native_sim_socket_medium_msg zb_host_socket_coord_make_tx(
	uint16_t node_id, uint8_t channel, enum zb_host_socket_frame_type type,
	const char *model_id)
{
	static uint8_t wire[128];
	struct zb_native_sim_socket_medium_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX;
	msg.node_id = node_id;
	msg.channel = channel;
	msg.psdu = wire;
	msg.psdu_len = encode_frame(type, wire, 0x2700U, ZB_COORD_SHORT_ADDR, model_id);

	return msg;
}

void zb_host_socket_coord_init(struct zb_host_socket_coord *coord)
{
	memset(coord, 0, sizeof(*coord));
	coord->permit_join = true;
	coord->pan_id = ZB_PAN_ID;
	coord->next_child_short = 0x2700U;
	coord->child_short = ZB_NO_SHORT_ADDR;
	zb_native_sim_socket_medium_peer_reset(&coord->peer);
}

static void set_output(struct zb_host_socket_coord *coord,
		       struct zb_native_sim_socket_medium_msg *output,
		       enum zb_host_socket_frame_type type)
{
	memset(output, 0, sizeof(*output));
	output->type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX;
	output->node_id = coord->peer.node_id;
	output->channel = coord->peer.channel;
	output->rssi_dbm = -40;
	output->lqi = 255U;
	output->psdu = coord->output_psdu;
	output->psdu_len = encode_frame(type, coord->output_psdu, ZB_COORD_SHORT_ADDR,
					 coord->child_short, NULL);
	coord->output_psdu_len = output->psdu_len;
}

static void maybe_capture_model_id(struct zb_host_socket_coord *coord,
				   const struct zb_native_sim_socket_medium_msg *input)
{
	uint8_t str_len;

	if (input->psdu_len < 23U || get_le16(&input->psdu[18]) != ZB_ZCL_ATTR_MODEL_ID) {
		return;
	}

	str_len = input->psdu[22];
	if ((size_t)(23U + str_len) > input->psdu_len || str_len >= sizeof(coord->observed_model_id)) {
		return;
	}

	memcpy(coord->observed_model_id, &input->psdu[23], str_len);
	coord->observed_model_id[str_len] = '\0';
	coord->interview_complete = true;
}

int zb_host_socket_coord_process(struct zb_host_socket_coord *coord,
				 const struct zb_native_sim_socket_medium_msg *input,
				 struct zb_native_sim_socket_medium_msg *output)
{
	enum zb_host_socket_frame_type type;
	enum zb_host_socket_frame_type queued;

	if (input == NULL) {
		return 0;
	}

	if (input->type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER ||
	    input->type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO) {
		(void)zb_native_sim_socket_medium_peer_apply(&coord->peer, input);
		return 0;
	}

	type = zb_host_socket_coord_identify_frame(input->psdu, input->psdu_len);

	switch (type) {
	case ZB_HOST_SOCKET_FRAME_BEACON_REQ:
		if (output != NULL) {
			set_output(coord, output, ZB_HOST_SOCKET_FRAME_BEACON);
			return 1;
		}
		return 0;
	case ZB_HOST_SOCKET_FRAME_ASSOC_REQ:
		coord->last_assoc_status = coord->permit_join ? 0U : 1U;
		/*
		 * Cap byte is the very last byte of the AssocReq command
		 * payload. Bit 3 (0x08) = rx-on-when-idle. Routers / FFDs
		 * set it so the coord can push frames unsolicited; EDs
		 * clear it and rely on DataRequest polling instead.
		 */
		coord->deliver_queued_unsolicited =
			(input->psdu_len > 0U) &&
			((input->psdu[input->psdu_len - 1U] & 0x08U) != 0U);
		if (coord->permit_join) {
			coord->child_short = coord->next_child_short++;
			queue_frame(coord, ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY);
		}
		if (output != NULL) {
			memset(output, 0, sizeof(*output));
			output->type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX;
			output->node_id = coord->peer.node_id;
			output->channel = coord->peer.channel;
			output->rssi_dbm = -40;
			output->lqi = 255U;
			output->psdu = coord->output_psdu;
			output->psdu_len = encode_frame(ZB_HOST_SOCKET_FRAME_ASSOC_RSP,
							coord->output_psdu,
							ZB_COORD_SHORT_ADDR,
							coord->permit_join ? coord->child_short :
									ZB_NO_SHORT_ADDR,
							NULL);
			coord->output_psdu[12] = coord->last_assoc_status;
			return 1;
		}
		return 0;
	case ZB_HOST_SOCKET_FRAME_DATA_REQ:
		if (output != NULL && pop_frame(coord, &queued)) {
			set_output(coord, output, queued);
			return 1;
		}
		return 0;
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ:
		coord->got_timeout_req = true;
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP);
		return 0;
	case ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE:
		coord->got_device_announce = true;
		/* Z2M interviews Node Descriptor first — mirror that order so we
		 * reproduce the real failing step on native_sim. */
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_NODE_DESC_REQ);
		return 0;
	case ZB_HOST_SOCKET_FRAME_NODE_DESC_RSP:
		coord->got_node_desc_rsp = true;
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ);
		return 0;
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP:
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ);
		return 0;
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP:
		queue_frame(coord, ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ);
		return 0;
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP:
		maybe_capture_model_id(coord, input);
		return 0;
	default:
		return 0;
	}
}

void zb_host_socket_coord_observed_model_id(const struct zb_host_socket_coord *coord,
					    char *buffer, size_t buffer_len)
{
	if (buffer_len == 0U) {
		return;
	}

	strncpy(buffer, coord->observed_model_id, buffer_len - 1U);
	buffer[buffer_len - 1U] = '\0';
}

uint8_t zb_host_socket_coord_last_assoc_status(const struct zb_host_socket_coord *coord)
{
	return coord->last_assoc_status;
}

int zb_host_socket_coord_drain_unsolicited(struct zb_host_socket_coord *coord,
					    struct zb_native_sim_socket_medium_msg *output)
{
	enum zb_host_socket_frame_type queued;

	if (coord == NULL || output == NULL) {
		return 0;
	}

	if (!coord->deliver_queued_unsolicited) {
		return 0;
	}

	if (!pop_frame(coord, &queued)) {
		return 0;
	}

	set_output(coord, output, queued);
	return 1;
}
