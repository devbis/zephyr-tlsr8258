/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"
#include "mac/includes/mac_phy.h"
#include "os/ev_timer.h"

#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_config.h>
#include <zephyr/zigbee/zb_radio_port.h>

#include "zb_minimal_ccm.h"

LOG_MODULE_REGISTER(zigbee_nwk_ed_minimal, CONFIG_ZIGBEE_LOG_LEVEL);

#define NWK_ED_MINIMAL_SCAN_WINDOW_MIN_MS 3000U
#define NWK_ED_MINIMAL_SCAN_WINDOW_MAX_MS 5000U
#define NWK_ED_MINIMAL_JOIN_POLL_MS       200U
#define NWK_ED_MINIMAL_JOIN_POLL_MAX      20U
#define NWK_ED_MINIMAL_INTERVIEW_POLL_MS  200U
#define NWK_ED_MINIMAL_INTERVIEW_POLL_MAX 20U
#define NWK_ED_MINIMAL_TIMEOUT_REQ_DELAY_MS 200U
#define NWK_ED_MINIMAL_RX_EVT_Q_LEN       4U
#define NWK_ED_MINIMAL_NWK_AUX_HDR_LEN    14U
#define NWK_ED_MINIMAL_NWK_MIC_LEN        4U
#define NWK_ED_MINIMAL_NWK_SEC_CTRL       0x2DU
#define NWK_ED_MINIMAL_NWK_SEC_CTRL_WIRE  0x28U
#define NWK_ED_MINIMAL_NWK_TIMEOUT_REQ_CMD_ID 0x0BU
#define NWK_ED_MINIMAL_NWK_TIMEOUT_RSP_CMD_ID 0x0CU

typedef enum {
	NWK_ED_MINIMAL_STATE_IDLE = 0,
	NWK_ED_MINIMAL_STATE_DISCOVERY,
	NWK_ED_MINIMAL_STATE_JOINING,
	NWK_ED_MINIMAL_STATE_REJOIN,
	NWK_ED_MINIMAL_STATE_INTERVIEW,
} nwk_ed_minimal_state_t;

typedef enum {
	NWK_ED_MINIMAL_RX_EVT_NONE = 0,
	NWK_ED_MINIMAL_RX_EVT_BEACON,
	NWK_ED_MINIMAL_RX_EVT_TRAFFIC_CANDIDATE,
	NWK_ED_MINIMAL_RX_EVT_ASSOC_RSP,
} nwk_ed_minimal_rx_evt_type_t;

typedef struct {
	bool initialized;
	bool warmStart;
	nwk_ed_minimal_state_t state;
	u32 lastScanChannels;
	u8 lastScanDuration;
	u32 lastRejoinScanChannels;
	u8 lastRejoinScanDuration;
	bool rejoinWithBackoff;
	bool parentCandidateValid;
	u16 parentCandidateShortAddr;
	addrExt_t parentCandidateIeee;
	u8 lastJoinStatus;
	bool fixedJoinValid;
	u8 fixedJoinChannel;
	u16 fixedJoinPanId;
	u16 fixedJoinShortAddr;
	extPANId_t fixedJoinExtPanId;
	u8 fixedJoinNwkKey[SEC_KEY_LEN];
	addrExt_t fixedJoinTcAddr;

	u32 remainingScanChannels;
	u8 activeScanChannel;
	bool haveBeaconCandidate;
	s8 bestBeaconRssi;
	u16 candidatePanId;
	u16 candidateShortAddr;
	extPANId_t candidateExtPanId;
	bool discoveryForRejoin;
	u8 activeChannel;
	u16 activePanId;
	u16 activeShortAddr;
	u16 activeParentShortAddr;
	extPANId_t activeExtPanId;
	u8 assocPollCount;
	bool interviewRejoinMode;
	u8 interviewPollCount;
	u32 interviewPollIntervalMs;
	bool endDevTimeoutRspSeen;
	bool endDevTimeoutReqScheduled;
	u32 rxEvtDropCount;
	u32 rxEvtOverflowCount;
	nwk_ed_minimal_rx_evt_type_t lastRxEvtDropType;

	ev_timer_event_t opTimer;
	ev_timer_event_t timeoutReqTimer;
} nwk_ed_minimal_ctx_t;

static nwk_ed_minimal_ctx_t g_nwkEdCtx;
static struct k_spinlock g_nwkEdRxEvtLock;

typedef struct {
	nwk_ed_minimal_rx_evt_type_t type;
	s8 rssi;
	union {
		struct {
			u16 panId;
			u16 parentShortAddr;
			extPANId_t extPanId;
			u8 channel;
		} beacon;
		struct {
			u16 panId;
			u16 parentShortAddr;
			u8 channel;
		} traffic;
		struct {
			u8 macStatus;
			u16 assignedShortAddr;
			bool srcExtValid;
			addrExt_t srcExtAddr;
			bool dstExtValid;
			addrExt_t dstExtAddr;
			bool dstShortValid;
			u16 dstShortAddr;
			bool srcShortValid;
			u16 srcShortAddr;
		} assocRsp;
	};
} nwk_ed_minimal_rx_evt_t;

static nwk_ed_minimal_rx_evt_t g_nwkEdRxEvtQ[NWK_ED_MINIMAL_RX_EVT_Q_LEN];
static u8 g_nwkEdRxEvtHead;
static u8 g_nwkEdRxEvtTail;
static u8 g_nwkEdRxEvtCount;
volatile u32 zb_nwk_ed_trace[16] = {0x4e574b45U};

static void nwk_ed_minimal_joined_idle_poll_schedule(u32 timeoutMs);
static void nwk_ed_minimal_timeout_req_schedule(u32 timeoutMs);

extern void tl_zdoEdMinimalDiscoveryDone(u8 status);
extern void tl_zdoEdMinimalJoinDone(u8 status, bool rejoinMode);
extern void bdb_ed_runtime_join_complete(void);
extern u8 zb_zdoSendDevAnnance(void);

static u16 nwk_ed_minimal_u16_from_le(const u8 *buf)
{
	return ((u16)buf[1] << 8) | (u16)buf[0];
}

static bool nwk_ed_minimal_channel_mask_contains(u32 mask, u8 ch)
{
	return (ch <= 31U) && ((mask & ((u32)1U << ch)) != 0U);
}

static u8 nwk_ed_minimal_next_scan_channel(u32 mask, u8 startExclusive)
{
	for (u8 ch = (u8)(startExclusive + 1U); ch <= TL_ZB_MAC_CHANNEL_STOP; ch++) {
		if (nwk_ed_minimal_channel_mask_contains(mask, ch)) {
			return ch;
		}
	}

	return 0xFFU;
}

static u32 nwk_ed_minimal_scan_window_ms(u8 scanDuration)
{
	u8 bounded = (scanDuration > 8U) ? 8U : scanDuration;
	u32 window = 16U * (((u32)1U << bounded) + 1U);

	if (window < NWK_ED_MINIMAL_SCAN_WINDOW_MIN_MS) {
		return NWK_ED_MINIMAL_SCAN_WINDOW_MIN_MS;
	}
	if (window > NWK_ED_MINIMAL_SCAN_WINDOW_MAX_MS) {
		return NWK_ED_MINIMAL_SCAN_WINDOW_MAX_MS;
	}

	return window;
}

static void nwk_ed_minimal_timer_cancel(void)
{
	ev_unon_timer(&g_nwkEdCtx.opTimer);
}

static void nwk_ed_minimal_timeout_req_cancel(void)
{
	ev_unon_timer(&g_nwkEdCtx.timeoutReqTimer);
}

static void nwk_ed_minimal_timer_start(u32 timeoutMs);
static void nwk_ed_minimal_joined_idle_poll_restart(u32 timeoutMs);
static void nwk_ed_minimal_post_join_poll_task(void *arg);
static void nwk_ed_minimal_post_join_announce_task(void *arg);
static void nwk_ed_minimal_timeout_req_task(void *arg);
static void nwk_ed_minimal_rx_event_task(void *arg);
static void nwk_ed_minimal_repair_joined_context_if_needed(void);
void tl_zbNwkEdMinimalInterviewPollStart(u8 count, u32 intervalMs);
void tl_zbNwkEdMinimalPollEnsure(void);

static u32 nwk_ed_minimal_effective_poll_rate(void)
{
	u32 rate = zdo_af_get_syn_rate();

	return (rate != 0U) ? rate : POLL_RATE;
}

static void nwk_ed_minimal_sync_zb_info_from_runtime(void)
{
	g_zbInfo.macPib = g_zbMacPib;
	g_zbInfo.nwkNib = g_zbNIB;
}

typedef struct {
	u16 fcf;
	u8 frameType;
	u8 srcAddrMode;
	u8 dstAddrMode;
	u8 headerLen;
	bool srcPanValid;
	u16 srcPanId;
	bool srcExtValid;
	addrExt_t srcExtAddr;
	bool srcShortValid;
	u16 srcShortAddr;
	bool dstShortValid;
	u16 dstShortAddr;
	bool dstExtValid;
	addrExt_t dstExtAddr;
} nwk_ed_minimal_mac_hdr_t;

static bool nwk_ed_minimal_parse_mac_header(const u8 *psdu, u8 len, nwk_ed_minimal_mac_hdr_t *out)
{
	u16 fcf;
	u8 idx = MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN;
	u16 dstPanId = MAC_INVALID_PANID;

	if (psdu == NULL || out == NULL || len < idx) {
		return FALSE;
	}

	memset(out, 0, sizeof(*out));
	fcf = nwk_ed_minimal_u16_from_le(psdu);
	out->fcf = fcf;
	out->frameType = (u8)((fcf & MAC_FCF_FRAME_TYPE_MASK) >> MAC_FCF_FRAME_TYPE_POS);
	out->dstAddrMode = (u8)((fcf & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	out->srcAddrMode = (u8)((fcf & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	if (out->dstAddrMode != ZB_ADDR_NO_ADDR) {
		u8 dstAddrLen;

		if (idx + MAC_PAN_ID_FIELD_LEN > len) {
			return FALSE;
		}
		dstPanId = nwk_ed_minimal_u16_from_le(&psdu[idx]);
		idx += MAC_PAN_ID_FIELD_LEN;

		if (out->dstAddrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
			dstAddrLen = MAC_SHORT_ADDR_FIELD_LEN;
			if (idx + dstAddrLen > len) {
				return FALSE;
			}
			out->dstShortAddr = nwk_ed_minimal_u16_from_le(&psdu[idx]);
			out->dstShortValid = TRUE;
		} else if (out->dstAddrMode == ZB_ADDR_64BIT_DEV) {
			dstAddrLen = MAC_EXT_ADDR_FIELD_LEN;
			if (idx + dstAddrLen > len) {
				return FALSE;
			}
			memcpy(out->dstExtAddr, &psdu[idx], sizeof(out->dstExtAddr));
			out->dstExtValid = TRUE;
		} else {
			return FALSE;
		}

		idx += dstAddrLen;
	}

	if (out->srcAddrMode != ZB_ADDR_NO_ADDR) {
		u8 srcAddrLen;

		if ((fcf & MAC_FCF_INTRA_PAN_MASK) != 0U && out->dstAddrMode != ZB_ADDR_NO_ADDR) {
			out->srcPanId = dstPanId;
			out->srcPanValid = TRUE;
		} else {
			if (idx + MAC_PAN_ID_FIELD_LEN > len) {
				return FALSE;
			}
			out->srcPanId = nwk_ed_minimal_u16_from_le(&psdu[idx]);
			out->srcPanValid = TRUE;
			idx += MAC_PAN_ID_FIELD_LEN;
		}

		if (out->srcAddrMode == ZB_ADDR_16BIT_DEV_OR_BROADCAST) {
			srcAddrLen = MAC_SHORT_ADDR_FIELD_LEN;
			if (idx + srcAddrLen > len) {
				return FALSE;
			}
			out->srcShortAddr = nwk_ed_minimal_u16_from_le(&psdu[idx]);
			out->srcShortValid = TRUE;
		} else if (out->srcAddrMode == ZB_ADDR_64BIT_DEV) {
			srcAddrLen = MAC_EXT_ADDR_FIELD_LEN;
			if (idx + srcAddrLen > len) {
				return FALSE;
			}
			memcpy(out->srcExtAddr, &psdu[idx], sizeof(out->srcExtAddr));
			out->srcExtValid = TRUE;
		} else {
			return FALSE;
		}

		idx += srcAddrLen;
	}

	out->headerLen = idx;
	return TRUE;
}

static void nwk_ed_minimal_apply_tc_context(void)
{
	const u8 *tcAddr = NULL;
	bool centralized = FALSE;

	if (!ZB_IEEE_ADDR_IS_ZERO(g_nwkEdCtx.fixedJoinTcAddr) &&
	    !ZB_IEEE_ADDR_IS_INVALID(g_nwkEdCtx.fixedJoinTcAddr)) {
		tcAddr = g_nwkEdCtx.fixedJoinTcAddr;
		centralized = TRUE;
	} else if (!ZB_IEEE_ADDR_IS_ZERO(ss_ib.trust_center_address) &&
		   !ZB_IEEE_ADDR_IS_INVALID(ss_ib.trust_center_address)) {
		tcAddr = ss_ib.trust_center_address;
		centralized = TRUE;
	}

	if (centralized) {
		ss_securityModeSet(SS_SEMODE_CENTRALIZED);
		ZB_IEEE_ADDR_COPY(ss_ib.trust_center_address, tcAddr);
		LOG_INF("join security: centralized tc=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
			tcAddr[0], tcAddr[1], tcAddr[2], tcAddr[3],
			tcAddr[4], tcAddr[5], tcAddr[6], tcAddr[7]);
	} else {
		ZB_IEEE_ADDR_INVALID(ss_ib.trust_center_address);
		ss_securityModeSet(SS_SEMODE_DISTRIBUTED);
		LOG_INF("join security: distributed/no tc");
	}
}

static bool nwk_ed_minimal_parse_beacon_candidate(const u8 *psdu, u8 len,
						  const nwk_ed_minimal_mac_hdr_t *hdr,
						  u16 *panId, u16 *coordShortAddr, extPANId_t extPanId)
{
	u8 idx;
	u8 gtsSpec;
	u8 pendingSpec;
	u8 gtsDescCount;
	u8 pendingShortCount;
	u8 pendingExtCount;
	u8 superframeSpec2;

	if (psdu == NULL || hdr == NULL || panId == NULL || coordShortAddr == NULL ||
	    extPanId == NULL || !hdr->srcPanValid || !hdr->srcShortValid) {
		return FALSE;
	}

	idx = hdr->headerLen;
	if (idx + 3U > len) {
		return FALSE;
	}

	/*
	 * Vendor discovery path does not reject beacons solely because the
	 * association-permit bit is clear. It only requires the upper
	 * superframe/capability byte to carry non-zero network information and
	 * lets the later association attempt determine if joining is currently
	 * permitted.
	 */
	superframeSpec2 = psdu[idx + 1U];
	if ((superframeSpec2 & 0x7FU) == 0U) {
		return FALSE;
	}

	idx += 2U;
	gtsSpec = psdu[idx++];
	gtsDescCount = (u8)(gtsSpec & 0x07U);
	if (gtsDescCount > 0U) {
		u8 gtsLen = (u8)(1U + (gtsDescCount * 3U));
		if (idx + gtsLen > len) {
			return FALSE;
		}
		idx = (u8)(idx + gtsLen);
	}

	if (idx + 1U > len) {
		return FALSE;
	}
	pendingSpec = psdu[idx++];
	pendingShortCount = (u8)(pendingSpec & 0x07U);
	pendingExtCount = (u8)((pendingSpec >> 4) & 0x07U);

	if (idx + (pendingShortCount * 2U) + (pendingExtCount * 8U) + 11U > len) {
		return FALSE;
	}
	idx = (u8)(idx + (pendingShortCount * 2U) + (pendingExtCount * 8U));

	/* Beacon payload: protocol_id(1), stack/version(1), capacity/depth(1), extPAN(8), ... */
	memcpy(extPanId, &psdu[idx + 3U], sizeof(extPANId_t));
	*panId = hdr->srcPanId;
	*coordShortAddr = hdr->srcShortAddr;

	return TRUE;
}

static void nwk_ed_minimal_channel_set(u8 channel)
{
	(void)zb_radio_port_set_channel(channel);
	(void)tl_zbMacAttrSet(MAC_PHY_ATTR_CURRENT_CHANNEL, &channel, sizeof(channel));
}

static u8 *nwk_ed_minimal_active_nwk_key_get(void)
{
	if (ss_ib.activeSecureMaterialIndex >= SECUR_N_SECUR_MATERIAL) {
		return NULL;
	}

	return ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key;
}

static bool nwk_ed_minimal_key_is_set(const u8 *key)
{
	if (key == NULL) {
		return FALSE;
	}

	for (u8 i = 0U; i < SEC_KEY_LEN; i++) {
		if (key[i] != 0U) {
			return TRUE;
		}
	}

	return FALSE;
}

static void nwk_ed_minimal_install_fixed_join_key_if_needed(void)
{
	ss_material_set_t *material;

	if (!nwk_ed_minimal_key_is_set(g_nwkEdCtx.fixedJoinNwkKey)) {
		return;
	}
	if (nwk_ed_minimal_key_is_set(nwk_ed_minimal_active_nwk_key_get())) {
		return;
	}

	material = &ss_ib.nwkSecurMaterialSet[0];
	memcpy(material->key, g_nwkEdCtx.fixedJoinNwkKey, SEC_KEY_LEN);
	material->keySeqNum = 0U;
	material->keyType = 1U;
	ss_ib.activeSecureMaterialIndex = 0U;
	ss_ib.activeKeySeqNum = 0U;
	ss_ib.preConfiguredKeyType |= SS_PRECONFIGURED_NWKKEY;
	aps_ib.aps_authenticated = 1U;
}

static u8 nwk_ed_minimal_timeout_req_value_get(void)
{
	if (g_zbNIB.endDevTimeoutDefault < REQTIMEOUTENUM_INVALID) {
		return g_zbNIB.endDevTimeoutDefault;
	}

	return NWK_ENDDEV_TIMEOUT_DEFAULT;
}

static bool nwk_ed_minimal_send_timeout_request(void)
{
	u8 frame[127];
	u8 nonce[13];
	u8 *key;
	u8 timeoutReq;
	u8 enc_len;
	u8 nwkHdrLen;
	u8 *payload;
	u32 frameCounter;
	u8 keySeq;
	u8 idx = 0U;
	u16 nwkFcf = 0U;
	u16 macFcf = 0U;
	int rc;

	if (!g_zbNwkCtx.joined || g_nwkEdCtx.activePanId == MAC_INVALID_PANID ||
	    g_nwkEdCtx.activeParentShortAddr == MAC_SHORT_ADDR_NONE ||
	    g_zbNIB.nwkAddr >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED) {
		return FALSE;
	}

	key = nwk_ed_minimal_active_nwk_key_get();
	if (key == NULL) {
		LOG_WRN("joined TX: timeout req skipped, no active nwk key");
		return FALSE;
	}

	macFcf |= MAC_FRAME_DATA;
	macFcf |= MAC_FCF_ACK_REQ_BIT;
	macFcf |= MAC_FCF_INTRA_PAN_MASK;
	macFcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_DST_ADDR_MODE_POS;
	macFcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_SRC_ADDR_MODE_POS;

	COPY_U16TOBUFFER(&frame[idx], macFcf);
	idx += MAC_FCF_FIELD_LEN;
	frame[idx++] = ZB_MAC_DSN();
	ZB_INC_MAC_DSN();
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activePanId);
	idx += MAC_PAN_ID_FIELD_LEN;
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activeParentShortAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;
	COPY_U16TOBUFFER(&frame[idx], g_zbNIB.nwkAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;

	nwkFcf |= (u16)FRAME_TYPE_COMMAND;
	nwkFcf |= (u16)(0x02U << 2);
	nwkFcf |= BIT(9);
	nwkFcf |= BIT(13);

	COPY_U16TOBUFFER(&frame[idx], nwkFcf);
	idx += 2U;
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activeParentShortAddr);
	idx += 2U;
	COPY_U16TOBUFFER(&frame[idx], g_zbNIB.nwkAddr);
	idx += 2U;
	frame[idx++] = 1U;
	frame[idx++] = g_zbNIB.seqNum++;
	nwkHdrLen = (u8)(8U + NWK_ED_MINIMAL_NWK_AUX_HDR_LEN);
	frame[idx++] = NWK_ED_MINIMAL_NWK_SEC_CTRL;
	frameCounter = ss_ib.outgoingFrameCounter++;
	COPY_U32TOBUFFER(&frame[idx], frameCounter);
	idx += 4U;
	memcpy(&frame[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
	idx += sizeof(addrExt_t);
	keySeq = ss_ib.activeKeySeqNum;
	frame[idx++] = keySeq;

	timeoutReq = nwk_ed_minimal_timeout_req_value_get();
	payload = &frame[idx];
	payload[0] = NWK_ED_MINIMAL_NWK_TIMEOUT_REQ_CMD_ID;
	payload[1] = timeoutReq;
	payload[2] = 0U;

	memcpy(nonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
	COPY_U32TOBUFFER(&nonce[8], frameCounter);
	nonce[12] = NWK_ED_MINIMAL_NWK_SEC_CTRL;
	enc_len = zb_minimal_ccm_encrypt_auth(key, nonce, NWK_ED_MINIMAL_NWK_MIC_LEN,
					      &frame[idx - nwkHdrLen], nwkHdrLen, payload, 3U,
					      &payload[3]);
	if (enc_len != (u8)(3U + NWK_ED_MINIMAL_NWK_MIC_LEN)) {
		LOG_WRN("joined TX: timeout req encrypt failed len=%u", enc_len);
		return FALSE;
	}

	frame[idx - NWK_ED_MINIMAL_NWK_AUX_HDR_LEN] = NWK_ED_MINIMAL_NWK_SEC_CTRL_WIRE;
	idx = (u8)(idx + enc_len);
	(void)nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);

	rc = zb_platform_radio_send_raw_psdu(frame, idx);
	if (rc < 0) {
		LOG_WRN("joined TX: timeout req tx failed rc=%d", rc);
		return FALSE;
	}

	LOG_INF("joined TX: timeout req parent=0x%04x timeout=%u key=%u fc=%u",
		g_nwkEdCtx.activeParentShortAddr, timeoutReq, keySeq, frameCounter);
	return TRUE;
}

static bool nwk_ed_minimal_get_join_profile(struct zb_platform_bdb_join_profile *profile)
{
	if (profile == NULL) {
		return FALSE;
	}

	memset(profile, 0, sizeof(*profile));
	return zb_platform_app_get_join_profile(profile) ? TRUE : FALSE;
}

static bool nwk_ed_minimal_beacon_matches_join_profile(
	const nwk_ed_minimal_rx_evt_t *evt,
	const struct zb_platform_bdb_join_profile *profile)
{
	if (evt == NULL || profile == NULL) {
		return FALSE;
	}

	if (profile->pan_id_valid && evt->beacon.panId != profile->pan_id) {
		return FALSE;
	}

	if (profile->ext_pan_id_valid &&
	    memcmp(evt->beacon.extPanId, profile->ext_pan_id, sizeof(evt->beacon.extPanId)) != 0) {
		return FALSE;
	}

	return TRUE;
}

static void nwk_ed_minimal_rx_evt_drop_record(nwk_ed_minimal_rx_evt_type_t type)
{
	g_nwkEdCtx.rxEvtDropCount++;
	g_nwkEdCtx.rxEvtOverflowCount++;
	g_nwkEdCtx.lastRxEvtDropType = type;
	g_sysDiags.phytoMACqueuelimitreached++;

	if (g_nwkEdCtx.rxEvtOverflowCount == 1U ||
	    (g_nwkEdCtx.rxEvtOverflowCount % 16U) == 0U) {
		LOG_WRN("RX event queue full: dropped type=%u total=%u",
			(u8)type, g_nwkEdCtx.rxEvtOverflowCount);
	}
}

static bool nwk_ed_minimal_rx_evt_push(const nwk_ed_minimal_rx_evt_t *evt)
{
	k_spinlock_key_t key;

	if (evt == NULL) {
		return FALSE;
	}

	key = k_spin_lock(&g_nwkEdRxEvtLock);
	if (g_nwkEdRxEvtCount >= NWK_ED_MINIMAL_RX_EVT_Q_LEN) {
		k_spin_unlock(&g_nwkEdRxEvtLock, key);
		nwk_ed_minimal_rx_evt_drop_record(evt->type);
		return FALSE;
	}

	g_nwkEdRxEvtQ[g_nwkEdRxEvtHead] = *evt;
	g_nwkEdRxEvtHead = (u8)((g_nwkEdRxEvtHead + 1U) % NWK_ED_MINIMAL_RX_EVT_Q_LEN);
	g_nwkEdRxEvtCount++;
	k_spin_unlock(&g_nwkEdRxEvtLock, key);

	return TRUE;
}

static bool nwk_ed_minimal_rx_evt_pop(nwk_ed_minimal_rx_evt_t *evt)
{
	k_spinlock_key_t key;

	if (evt == NULL) {
		return FALSE;
	}

	key = k_spin_lock(&g_nwkEdRxEvtLock);
	if (g_nwkEdRxEvtCount == 0U) {
		k_spin_unlock(&g_nwkEdRxEvtLock, key);
		return FALSE;
	}

	*evt = g_nwkEdRxEvtQ[g_nwkEdRxEvtTail];
	g_nwkEdRxEvtTail = (u8)((g_nwkEdRxEvtTail + 1U) % NWK_ED_MINIMAL_RX_EVT_Q_LEN);
	g_nwkEdRxEvtCount--;
	k_spin_unlock(&g_nwkEdRxEvtLock, key);

	return TRUE;
}

static void nwk_ed_minimal_finish_discovery(u8 status)
{
	bool rejoinFlow = g_nwkEdCtx.discoveryForRejoin;
	zb_nwk_ed_trace[1] = ((u32)g_nwkEdCtx.state << 24) | status;
	nwk_ed_minimal_timer_cancel();
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.discoveryForRejoin = FALSE;
	g_nwkEdCtx.lastJoinStatus = status;

	if (!rejoinFlow) {
		tl_zdoEdMinimalDiscoveryDone(status);
	}
}

static void nwk_ed_minimal_finish_join(u8 status, bool rejoinMode)
{
	zb_nwk_ed_trace[1] = ((u32)g_nwkEdCtx.state << 24) | ((u32)rejoinMode << 8) | status;
	nwk_ed_minimal_timer_cancel();
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
	g_nwkEdCtx.discoveryForRejoin = FALSE;
	g_nwkEdCtx.lastJoinStatus = status;

	if (status != ZDO_SUCCESS && !rejoinMode) {
		g_zbNwkCtx.joined = 0;
	}

	tl_zdoEdMinimalJoinDone(status, rejoinMode);
}

static void nwk_ed_minimal_complete_join(bool rejoinMode)
{
	g_zbNwkCtx.joined = 1U;
	g_zbNwkCtx.is_factory_new = 0U;
	g_zbNwkCtx.parentIsChanged = 0U;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.user_state = NLME_IDLE;
	zb_info_save(NULL);

	LOG_INF("%s complete: short 0x%04x pan 0x%04x parent 0x%04x",
		rejoinMode ? "rejoin" : "join", g_zbNIB.nwkAddr, g_zbNIB.panId,
		g_zbMacPib.coordShortAddress);
	nwk_ed_minimal_finish_join(ZDO_SUCCESS, rejoinMode);
	(void)TL_SCHEDULE_TASK(nwk_ed_minimal_post_join_poll_task, NULL);
	nwk_ed_minimal_joined_idle_poll_schedule(nwk_ed_minimal_effective_poll_rate());

	if (!rejoinMode) {
		(void)TL_SCHEDULE_TASK(nwk_ed_minimal_post_join_announce_task, NULL);
	}
}

static void nwk_ed_minimal_enter_interview(bool rejoinMode)
{
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_INTERVIEW;
	g_nwkEdCtx.interviewRejoinMode = rejoinMode;
	g_nwkEdCtx.assocPollCount = 0U;
	nwk_ed_minimal_timer_start(NWK_ED_MINIMAL_INTERVIEW_POLL_MS);
}

static bool nwk_ed_minimal_send_data_request(void)
{
	u8 frame[MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN + MAC_PAN_ID_FIELD_LEN +
		 MAC_SHORT_ADDR_FIELD_LEN + MAC_EXT_ADDR_FIELD_LEN + 1U];
	u8 idx = 0;
	u16 fcf = 0;
	bool useShortSrc = (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW || g_zbNwkCtx.joined) &&
			   (g_zbMacPib.shortAddress < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED);
	int rc;

	fcf |= MAC_FRAME_COMMAND;
	fcf |= MAC_FCF_ACK_REQ_BIT;
	fcf |= MAC_FCF_INTRA_PAN_MASK;
	fcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_DST_ADDR_MODE_POS;
	fcf |= (u16)(useShortSrc ? ZB_ADDR_16BIT_DEV_OR_BROADCAST : ZB_ADDR_64BIT_DEV)
	       << MAC_FCF_SRC_ADDR_MODE_POS;

	COPY_U16TOBUFFER(&frame[idx], fcf);
	idx += MAC_FCF_FIELD_LEN;
	frame[idx++] = ZB_MAC_DSN();
	ZB_INC_MAC_DSN();
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activePanId);
	idx += MAC_PAN_ID_FIELD_LEN;
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activeParentShortAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;
	if (useShortSrc) {
		COPY_U16TOBUFFER(&frame[idx], g_zbNIB.nwkAddr);
		idx += MAC_SHORT_ADDR_FIELD_LEN;
	} else {
		memcpy(&frame[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
		idx += sizeof(addrExt_t);
	}
	frame[idx++] = MAC_CMD_DATA_REQUEST;

	rc = zb_platform_radio_send_raw_psdu(frame, idx);
	zb_nwk_ed_trace[14]++;
	zb_nwk_ed_trace[15] = ((u32)(u8)idx << 24) | ((u32)MAC_CMD_DATA_REQUEST << 16) |
			       (u16)rc;
	if (rc < 0) {
		LOG_WRN("data request tx failed (rc=%d len=%u)", rc, idx);
		return FALSE;
	}
	return TRUE;
}

static bool nwk_ed_minimal_start_assoc(bool rejoinMode)
{
	u8 frame[MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN + MAC_PAN_ID_FIELD_LEN +
		 MAC_SHORT_ADDR_FIELD_LEN + MAC_EXT_ADDR_FIELD_LEN + 2U];
	u8 idx = 0;
	u16 fcf = 0;
	u8 capability = 0;
	u16 parentShortAddr;
	u16 panId;
	extPANId_t extPanId;
	u8 channel;
	int rc;

	if (g_nwkEdCtx.parentCandidateValid) {
		parentShortAddr = g_nwkEdCtx.parentCandidateShortAddr;
		panId = g_nwkEdCtx.candidatePanId;
		ZB_EXTPANID_COPY(extPanId, g_nwkEdCtx.candidateExtPanId);
		channel = g_nwkEdCtx.activeScanChannel;
	} else if (g_nwkEdCtx.fixedJoinValid) {
		parentShortAddr = g_nwkEdCtx.fixedJoinShortAddr;
		panId = g_nwkEdCtx.fixedJoinPanId;
		ZB_EXTPANID_COPY(extPanId, g_nwkEdCtx.fixedJoinExtPanId);
		channel = g_nwkEdCtx.fixedJoinChannel;
	} else {
		LOG_WRN("join start rejected: no parent candidate");
		return FALSE;
	}

	g_nwkEdCtx.activeChannel = channel;
	g_nwkEdCtx.activePanId = panId;
	g_nwkEdCtx.activeParentShortAddr = parentShortAddr;
	ZB_EXTPANID_COPY(g_nwkEdCtx.activeExtPanId, extPanId);
	g_nwkEdCtx.assocPollCount = 0U;

	nwk_ed_minimal_channel_set(channel);

	fcf |= MAC_FRAME_COMMAND;
	fcf |= MAC_FCF_ACK_REQ_BIT;
	fcf |= MAC_FCF_INTRA_PAN_MASK;
	fcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_DST_ADDR_MODE_POS;
	fcf |= (u16)ZB_ADDR_64BIT_DEV << MAC_FCF_SRC_ADDR_MODE_POS;

	COPY_U16TOBUFFER(&frame[idx], fcf);
	idx += MAC_FCF_FIELD_LEN;
	frame[idx++] = ZB_MAC_DSN();
	ZB_INC_MAC_DSN();
	COPY_U16TOBUFFER(&frame[idx], panId);
	idx += MAC_PAN_ID_FIELD_LEN;
	COPY_U16TOBUFFER(&frame[idx], parentShortAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;
	memcpy(&frame[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
	idx += sizeof(addrExt_t);
	frame[idx++] = MAC_CMD_ASSOCIATION_REQUEST;

	capability |= g_zbNIB.capabilityInfo.altPanCoord ? BIT(0) : 0U;
	capability |= g_zbNIB.capabilityInfo.devType ? BIT(1) : 0U;
	capability |= g_zbNIB.capabilityInfo.powerSrc ? BIT(2) : 0U;
	capability |= g_zbMacPib.rxOnWhenIdle ? BIT(3) : 0U;
	capability |= g_zbNIB.capabilityInfo.secuCapability ? BIT(6) : 0U;
	capability |= BIT(7); /* allocate short address */
	frame[idx++] = capability;

	rc = zb_platform_radio_send_raw_psdu(frame, idx);
	zb_nwk_ed_trace[11]++;
	zb_nwk_ed_trace[12] = ((u32)(u8)idx << 24) | ((u32)MAC_CMD_ASSOCIATION_REQUEST << 16) |
			       (u16)rc;
	zb_nwk_ed_trace[13] = ((u32)panId << 16) | parentShortAddr;
	if (rc < 0) {
		LOG_WRN("association request tx failed (rc=%d len=%u ch=%u)", rc, idx, channel);
		return FALSE;
	}

	g_nwkEdCtx.state = rejoinMode ? NWK_ED_MINIMAL_STATE_REJOIN : NWK_ED_MINIMAL_STATE_JOINING;
	nwk_ed_minimal_timer_start(NWK_ED_MINIMAL_JOIN_POLL_MS);
	LOG_INF("%s started: pan 0x%04x parent 0x%04x ch %u",
		rejoinMode ? "rejoin" : "join", panId, parentShortAddr, channel);

	return TRUE;
}

static void nwk_ed_minimal_send_beacon_request(void)
{
	int rc = zb_platform_radio_send_beacon_request();

	zb_nwk_ed_trace[4]++;
	zb_nwk_ed_trace[5] = ((u32)g_nwkEdCtx.activeScanChannel << 24) | (u16)rc;
	if (rc < 0) {
		LOG_WRN("beacon request tx failed (rc=%d ch=%u)", rc, g_nwkEdCtx.activeScanChannel);
	}
}

static bool nwk_ed_minimal_start_scan_channel(void)
{
	u8 nextChannel = nwk_ed_minimal_next_scan_channel(g_nwkEdCtx.remainingScanChannels,
							   g_nwkEdCtx.activeScanChannel);

	if (nextChannel == 0xFFU) {
		return FALSE;
	}

	g_nwkEdCtx.remainingScanChannels &= ~((u32)1U << nextChannel);
	g_nwkEdCtx.activeScanChannel = nextChannel;
	zb_nwk_ed_trace[2]++;
	zb_nwk_ed_trace[3] = ((u32)nextChannel << 24) | g_nwkEdCtx.remainingScanChannels;
	if (zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_RX, nextChannel) < 0) {
		LOG_WRN("discovery RX arm failed on channel %u", nextChannel);
	}
	nwk_ed_minimal_channel_set(nextChannel);
	nwk_ed_minimal_send_beacon_request();
	nwk_ed_minimal_timer_start(nwk_ed_minimal_scan_window_ms(g_nwkEdCtx.lastScanDuration));
	LOG_INF("discovery scanning channel %u", nextChannel);

	return TRUE;
}

static void nwk_ed_minimal_timer_task(void *arg)
{
	ARG_UNUSED(arg);

	/* Drain already queued RX events before making timeout/next-state decisions.
	 * Otherwise a beacon or association response that arrived just before the
	 * timer fires can be ignored until after we already conclude discovery/join
	 * has failed.
	 */
	nwk_ed_minimal_rx_event_task(NULL);

	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_DISCOVERY) {
		if (g_nwkEdCtx.discoveryForRejoin) {
			if (g_nwkEdCtx.haveBeaconCandidate || g_nwkEdCtx.parentCandidateValid) {
				if (!nwk_ed_minimal_start_assoc(TRUE)) {
					nwk_ed_minimal_finish_join(ZDO_NETWORK_LOST, TRUE);
				}
				return;
			}
		} else if (g_nwkEdCtx.haveBeaconCandidate) {
			nwk_ed_minimal_finish_discovery(ZDO_SUCCESS);
			return;
		}

		if (!nwk_ed_minimal_start_scan_channel()) {
			if (g_nwkEdCtx.discoveryForRejoin) {
				nwk_ed_minimal_finish_join(ZDO_NETWORK_LOST, TRUE);
			} else {
				LOG_WRN("discovery finished without beacon candidate");
				nwk_ed_minimal_finish_discovery(ZDO_NO_MATCH);
			}
		}
		return;
	}

	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_JOINING ||
	    g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN) {
		bool rejoinMode = (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN);

		if (g_nwkEdCtx.assocPollCount >= NWK_ED_MINIMAL_JOIN_POLL_MAX) {
			LOG_WRN("%s timed out waiting assoc response", rejoinMode ? "rejoin" : "join");
			nwk_ed_minimal_finish_join(rejoinMode ? ZDO_NETWORK_LOST : ZDO_TIMEOUT, rejoinMode);
			return;
		}

		if (!nwk_ed_minimal_send_data_request()) {
		}
		g_nwkEdCtx.assocPollCount++;
		nwk_ed_minimal_timer_start(NWK_ED_MINIMAL_JOIN_POLL_MS);
		return;
	}

	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW) {

		if (g_nwkEdCtx.assocPollCount >= NWK_ED_MINIMAL_INTERVIEW_POLL_MAX) {
			LOG_WRN("%s timed out waiting transport key",
				g_nwkEdCtx.interviewRejoinMode ? "rejoin" : "join");
			nwk_ed_minimal_finish_join(g_nwkEdCtx.interviewRejoinMode ?
						      ZDO_NETWORK_LOST : ZDO_TIMEOUT,
					       g_nwkEdCtx.interviewRejoinMode);
			return;
		}

		if (!nwk_ed_minimal_send_data_request()) {
		}
		g_nwkEdCtx.assocPollCount++;
		nwk_ed_minimal_timer_start(NWK_ED_MINIMAL_INTERVIEW_POLL_MS);
		return;
	}

	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_IDLE && g_zbNwkCtx.joined) {
		u32 nextPollMs = nwk_ed_minimal_effective_poll_rate();

		nwk_ed_minimal_repair_joined_context_if_needed();
		if (!nwk_ed_minimal_send_data_request()) {
		}

		if (g_nwkEdCtx.interviewPollCount != 0U) {
			g_nwkEdCtx.interviewPollCount--;
			if (g_nwkEdCtx.interviewPollCount != 0U) {
				nextPollMs = g_nwkEdCtx.interviewPollIntervalMs;
			}
		}

		nwk_ed_minimal_joined_idle_poll_schedule(nextPollMs);
	}
}

static int nwk_ed_minimal_timer_cb(void *arg)
{
	ARG_UNUSED(arg);

	if (TL_SCHEDULE_TASK(nwk_ed_minimal_timer_task, NULL) != RET_OK) {
		return 0;
	}
	return -1;
}

static int nwk_ed_minimal_timeout_req_timer_cb(void *arg)
{
	ARG_UNUSED(arg);
	if (TL_SCHEDULE_TASK(nwk_ed_minimal_timeout_req_task, NULL) != RET_OK) {
		return 0;
	}
	return -1;
}

static void nwk_ed_minimal_timer_start(u32 timeoutMs)
{
	if (g_nwkEdCtx.opTimer.cb == NULL) {
		memset(&g_nwkEdCtx.opTimer, 0, sizeof(g_nwkEdCtx.opTimer));
		g_nwkEdCtx.opTimer.cb = nwk_ed_minimal_timer_cb;
	}

	ev_on_timer(&g_nwkEdCtx.opTimer, timeoutMs);
}

static void nwk_ed_minimal_timeout_req_schedule(u32 timeoutMs)
{
	if (!g_zbNwkCtx.joined || g_nwkEdCtx.endDevTimeoutRspSeen) {
		return;
	}

	if (g_nwkEdCtx.timeoutReqTimer.cb == NULL) {
		memset(&g_nwkEdCtx.timeoutReqTimer, 0, sizeof(g_nwkEdCtx.timeoutReqTimer));
		g_nwkEdCtx.timeoutReqTimer.cb = nwk_ed_minimal_timeout_req_timer_cb;
	}

	g_nwkEdCtx.endDevTimeoutReqScheduled = TRUE;
	ev_on_timer(&g_nwkEdCtx.timeoutReqTimer, timeoutMs);
}

static void nwk_ed_minimal_joined_idle_poll_schedule(u32 timeoutMs)
{
	if (timeoutMs == 0U || !g_zbNwkCtx.joined ||
	    g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return;
	}

	nwk_ed_minimal_timer_start(timeoutMs);
}

static void nwk_ed_minimal_joined_idle_poll_restart(u32 timeoutMs)
{
	nwk_ed_minimal_repair_joined_context_if_needed();

	if (timeoutMs == 0U || !g_zbNwkCtx.joined ||
	    g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return;
	}

	nwk_ed_minimal_timer_start(timeoutMs);
}

static bool nwk_ed_minimal_has_restorable_join_context(void)
{
	return g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_IDLE &&
	       g_nwkEdCtx.activePanId != MAC_INVALID_PANID &&
	       g_nwkEdCtx.activeShortAddr < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED &&
	       g_nwkEdCtx.activeParentShortAddr < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED &&
	       nwk_ed_minimal_key_is_set(nwk_ed_minimal_active_nwk_key_get());
}

static void nwk_ed_minimal_repair_joined_context_if_needed(void)
{
	bool context_lost;

	if (!nwk_ed_minimal_has_restorable_join_context()) {
		return;
	}

	context_lost = g_zbMacPib.panId != g_nwkEdCtx.activePanId ||
		       g_zbMacPib.shortAddress != g_nwkEdCtx.activeShortAddr ||
		       g_zbMacPib.coordShortAddress != g_nwkEdCtx.activeParentShortAddr ||
		       g_zbNIB.panId != g_nwkEdCtx.activePanId ||
		       g_zbNIB.nwkAddr != g_nwkEdCtx.activeShortAddr;
	if (g_zbNwkCtx.joined && !context_lost) {
		return;
	}

	LOG_WRN("repair joined context: short 0x%04x pan 0x%04x parent 0x%04x",
		g_nwkEdCtx.activeShortAddr, g_nwkEdCtx.activePanId,
		g_nwkEdCtx.activeParentShortAddr);

	g_zbMacPib.panId = g_nwkEdCtx.activePanId;
	g_zbMacPib.shortAddress = g_nwkEdCtx.activeShortAddr;
	g_zbMacPib.coordShortAddress = g_nwkEdCtx.activeParentShortAddr;
	g_zbMacPib.associatedPanCoord = TRUE;
	g_zbMacPib.phyChannelCur = g_nwkEdCtx.activeChannel;
	g_zbNIB.panId = g_nwkEdCtx.activePanId;
	g_zbNIB.nwkAddr = g_nwkEdCtx.activeShortAddr;
	g_zbNIB.depth = 1U;
	ZB_EXTPANID_COPY(g_zbNIB.extPANId, g_nwkEdCtx.activeExtPanId);
	g_zbNwkCtx.joined = 1U;
	g_zbNwkCtx.is_factory_new = 0U;
	g_zbNwkCtx.parentIsChanged = 0U;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.user_state = NLME_IDLE;
	g_bdbAttrs.nodeIsOnANetwork = 1U;
	nwk_ed_minimal_sync_zb_info_from_runtime();
	zb_radio_port_update_filters(g_nwkEdCtx.activePanId, g_nwkEdCtx.activeShortAddr,
				     g_zbMacPib.extAddress);
	zb_info_save(NULL);
}

static void nwk_ed_minimal_runtime_reset(void)
{
	nwk_ed_minimal_timer_cancel();
	nwk_ed_minimal_timeout_req_cancel();
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.lastScanChannels = 0U;
	g_nwkEdCtx.lastScanDuration = 0U;
	g_nwkEdCtx.lastRejoinScanChannels = 0U;
	g_nwkEdCtx.lastRejoinScanDuration = 0U;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
	g_nwkEdCtx.parentCandidateValid = FALSE;
	g_nwkEdCtx.parentCandidateShortAddr = MAC_SHORT_ADDR_NONE;
	ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.parentCandidateIeee);
	g_nwkEdCtx.lastJoinStatus = ZDO_NOT_SUPPORTED;
	g_nwkEdCtx.remainingScanChannels = 0U;
	g_nwkEdCtx.activeScanChannel = 0xFFU;
	g_nwkEdCtx.haveBeaconCandidate = FALSE;
	g_nwkEdCtx.bestBeaconRssi = -127;
	g_nwkEdCtx.candidatePanId = MAC_INVALID_PANID;
	g_nwkEdCtx.candidateShortAddr = MAC_SHORT_ADDR_NONE;
	memset(g_nwkEdCtx.candidateExtPanId, 0, sizeof(g_nwkEdCtx.candidateExtPanId));
	g_nwkEdCtx.discoveryForRejoin = FALSE;
	g_nwkEdCtx.activeChannel = 0xFFU;
	g_nwkEdCtx.activePanId = MAC_INVALID_PANID;
	g_nwkEdCtx.activeShortAddr = MAC_SHORT_ADDR_BROADCAST;
	g_nwkEdCtx.activeParentShortAddr = MAC_SHORT_ADDR_NONE;
	memset(g_nwkEdCtx.activeExtPanId, 0, sizeof(g_nwkEdCtx.activeExtPanId));
	g_nwkEdCtx.assocPollCount = 0U;
	g_nwkEdCtx.interviewRejoinMode = FALSE;
	g_nwkEdCtx.interviewPollCount = 0U;
	g_nwkEdCtx.interviewPollIntervalMs = NWK_ED_MINIMAL_INTERVIEW_POLL_MS;
	g_nwkEdCtx.endDevTimeoutRspSeen = FALSE;
	g_nwkEdCtx.endDevTimeoutReqScheduled = FALSE;
	g_nwkEdCtx.rxEvtDropCount = 0U;
	g_nwkEdCtx.rxEvtOverflowCount = 0U;
	g_nwkEdCtx.lastRxEvtDropType = NWK_ED_MINIMAL_RX_EVT_NONE;
	g_nwkEdRxEvtHead = 0U;
	g_nwkEdRxEvtTail = 0U;
	g_nwkEdRxEvtCount = 0U;
}

static void nwk_ed_minimal_reset(bool warmStart)
{
	nwk_ed_minimal_runtime_reset();
	g_nwkEdCtx.initialized = TRUE;
	g_nwkEdCtx.warmStart = warmStart;
}

void tl_zbNwkEdMinimalRuntimeReset(void)
{
	nwk_ed_minimal_runtime_reset();
}

bool tl_zbNwkEdMinimalDiscoveryStart(u32 scanChannels, u8 scanDuration)
{
	u32 supportedMask = scanChannels & (((u32)1U << (TL_ZB_MAC_CHANNEL_STOP + 1U)) - 1U);

	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return FALSE;
	}
	if (supportedMask == 0U) {
		return FALSE;
	}

	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_DISCOVERY;
	zb_nwk_ed_trace[1] = ((u32)NWK_ED_MINIMAL_STATE_DISCOVERY << 24) | 1U;
	g_nwkEdCtx.discoveryForRejoin = FALSE;
	g_nwkEdCtx.lastScanChannels = scanChannels;
	g_nwkEdCtx.lastScanDuration = scanDuration;
	g_nwkEdCtx.remainingScanChannels = supportedMask;
	g_nwkEdCtx.activeScanChannel = 10U;
	g_nwkEdCtx.parentCandidateValid = FALSE;
	g_nwkEdCtx.haveBeaconCandidate = FALSE;
	g_nwkEdCtx.bestBeaconRssi = -127;
	g_nwkEdCtx.candidatePanId = MAC_INVALID_PANID;
	g_nwkEdCtx.candidateShortAddr = MAC_SHORT_ADDR_NONE;
	memset(g_nwkEdCtx.candidateExtPanId, 0, sizeof(g_nwkEdCtx.candidateExtPanId));

	if (!nwk_ed_minimal_start_scan_channel()) {
		g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
		return FALSE;
	}

	return TRUE;
}

void tl_zbNwkEdMinimalDiscoveryStop(void)
{
	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_DISCOVERY) {
		nwk_ed_minimal_finish_discovery(ZDO_TIMEOUT);
	}
}

bool tl_zbNwkEdMinimalAssocJoinStart(void)
{
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return FALSE;
	}

	return nwk_ed_minimal_start_assoc(FALSE);
}

bool tl_zbNwkEdMinimalRejoinStart(u32 scanChannels, u8 scanDuration, bool withBackoff)
{
	u32 supportedMask = scanChannels & (((u32)1U << (TL_ZB_MAC_CHANNEL_STOP + 1U)) - 1U);

	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE) {
		return FALSE;
	}

	g_nwkEdCtx.lastRejoinScanChannels = scanChannels;
	g_nwkEdCtx.lastRejoinScanDuration = scanDuration;
	g_nwkEdCtx.rejoinWithBackoff = withBackoff;

	if (g_nwkEdCtx.parentCandidateValid || g_nwkEdCtx.fixedJoinValid) {
		if (!nwk_ed_minimal_start_assoc(TRUE)) {
			g_nwkEdCtx.rejoinWithBackoff = FALSE;
			return FALSE;
		}
		return TRUE;
	}

	if (supportedMask == 0U) {
		g_nwkEdCtx.rejoinWithBackoff = FALSE;
		return FALSE;
	}

	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_DISCOVERY;
	zb_nwk_ed_trace[1] = ((u32)NWK_ED_MINIMAL_STATE_DISCOVERY << 24) | 2U;
	g_nwkEdCtx.discoveryForRejoin = TRUE;
	g_nwkEdCtx.lastScanChannels = scanChannels;
	g_nwkEdCtx.lastScanDuration = scanDuration;
	g_nwkEdCtx.remainingScanChannels = supportedMask;
	g_nwkEdCtx.activeScanChannel = 10U;
	g_nwkEdCtx.haveBeaconCandidate = FALSE;
	g_nwkEdCtx.bestBeaconRssi = -127;
	g_nwkEdCtx.candidatePanId = MAC_INVALID_PANID;
	g_nwkEdCtx.candidateShortAddr = MAC_SHORT_ADDR_NONE;
	memset(g_nwkEdCtx.candidateExtPanId, 0, sizeof(g_nwkEdCtx.candidateExtPanId));

	if (!nwk_ed_minimal_start_scan_channel()) {
		g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
		g_nwkEdCtx.discoveryForRejoin = FALSE;
		g_nwkEdCtx.rejoinWithBackoff = FALSE;
		return FALSE;
	}

	return TRUE;
}

void tl_zbNwkEdMinimalOperationAbort(void)
{
	nwk_ed_minimal_timer_cancel();
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
	g_nwkEdCtx.discoveryForRejoin = FALSE;
}

void tl_zbNwkEdMinimalOperationComplete(u8 status)
{
	g_nwkEdCtx.lastJoinStatus = status;
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
	g_nwkEdCtx.discoveryForRejoin = FALSE;
}

bool tl_zbNwkEdMinimalManagerIdle(void)
{
	return g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_IDLE;
}

bool tl_zbNwkEdMinimalCanProcessDataFrames(void)
{
	return g_zbNwkCtx.joined || (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW);
}

void tl_zbNwkEdMinimalTransportKeyDone(void)
{
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_INTERVIEW) {
		return;
	}

	nwk_ed_minimal_complete_join(g_nwkEdCtx.interviewRejoinMode);
}

static void nwk_ed_minimal_post_join_announce_task(void *arg)
{
	ARG_UNUSED(arg);

	if (!g_zbNwkCtx.joined) {
		return;
	}

	if (zb_zdoSendDevAnnance() == ZDO_SUCCESS) {
		tl_zbNwkEdMinimalInterviewPollStart(0U, 0U);
		nwk_ed_minimal_timeout_req_schedule(NWK_ED_MINIMAL_TIMEOUT_REQ_DELAY_MS);
	}
}

static void nwk_ed_minimal_post_join_poll_task(void *arg)
{
	ARG_UNUSED(arg);

	tl_zbNwkEdMinimalPollEnsure();
	bdb_ed_runtime_join_complete();
}

static void nwk_ed_minimal_timeout_req_task(void *arg)
{
	ARG_UNUSED(arg);

	g_nwkEdCtx.endDevTimeoutReqScheduled = FALSE;
	if (!g_zbNwkCtx.joined || g_nwkEdCtx.endDevTimeoutRspSeen) {
		return;
	}

	if (!nwk_ed_minimal_send_timeout_request()) {
		nwk_ed_minimal_timeout_req_schedule(NWK_ED_MINIMAL_TIMEOUT_REQ_DELAY_MS);
		return;
	}

	tl_zbNwkEdMinimalInterviewPollStart(0U, 0U);
}

void tl_zbNwkEdMinimalInterviewPollStart(u8 count, u32 intervalMs)
{
	if (!g_zbNwkCtx.joined || g_nwkEdCtx.activePanId == MAC_INVALID_PANID ||
	    g_nwkEdCtx.activeParentShortAddr == MAC_SHORT_ADDR_NONE) {
		return;
	}

	if (count == 0U) {
		count = NWK_ED_MINIMAL_INTERVIEW_POLL_MAX;
	}

	g_nwkEdCtx.interviewPollCount = count;
	g_nwkEdCtx.interviewPollIntervalMs =
		(intervalMs != 0U) ? intervalMs : NWK_ED_MINIMAL_INTERVIEW_POLL_MS;
	nwk_ed_minimal_joined_idle_poll_restart(1U);
}

void tl_zbNwkEdMinimalPollRestart(u32 timeoutMs)
{
	nwk_ed_minimal_repair_joined_context_if_needed();

	if (!g_zbNwkCtx.joined || g_nwkEdCtx.activePanId == MAC_INVALID_PANID ||
	    g_nwkEdCtx.activeParentShortAddr == MAC_SHORT_ADDR_NONE) {
		return;
	}

	if (timeoutMs == 0U) {
		timeoutMs = nwk_ed_minimal_effective_poll_rate();
	}

	nwk_ed_minimal_joined_idle_poll_restart(timeoutMs);
}

void tl_zbNwkEdMinimalPollEnsure(void)
{
	nwk_ed_minimal_repair_joined_context_if_needed();

	if (!g_zbNwkCtx.joined ||
	    g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_IDLE ||
	    g_nwkEdCtx.activePanId == MAC_INVALID_PANID ||
	    g_nwkEdCtx.activeParentShortAddr == MAC_SHORT_ADDR_NONE ||
	    !nwk_ed_minimal_key_is_set(nwk_ed_minimal_active_nwk_key_get())) {
		return;
	}

	(void)nwk_ed_minimal_send_data_request();
	nwk_ed_minimal_joined_idle_poll_restart(nwk_ed_minimal_effective_poll_rate());
}

void tl_zbNwkEdMinimalTimeoutRspReceived(u8 status, u8 parentInfo)
{
	g_nwkEdCtx.endDevTimeoutReqScheduled = FALSE;
	nwk_ed_minimal_timeout_req_cancel();
	g_zbNIB.parentInfo = parentInfo;

	if (status == TIMEOUT_RSP_STATUS_SUCCESS) {
		g_nwkEdCtx.endDevTimeoutRspSeen = TRUE;
		LOG_INF("joined RX: timeout rsp ok parentInfo=0x%02x", parentInfo);
		tl_zbNwkEdMinimalInterviewPollStart(0U, 0U);
	} else {
		LOG_WRN("joined RX: timeout rsp status=0x%02x parentInfo=0x%02x", status,
			parentInfo);
	}
}

u32 tl_zbNwkEdMinimalLastScanChannelsGet(void)
{
	return g_nwkEdCtx.lastScanChannels;
}

u32 tl_zbNwkEdMinimalLastRejoinScanChannelsGet(void)
{
	return g_nwkEdCtx.lastRejoinScanChannels;
}

void tl_zbNwkEdMinimalParentCandidateSet(u16 parentShortAddr, const addrExt_t parentIeeeAddr)
{
	g_nwkEdCtx.parentCandidateValid = TRUE;
	g_nwkEdCtx.parentCandidateShortAddr = parentShortAddr;
	if (parentIeeeAddr != NULL) {
		ZB_IEEE_ADDR_COPY(g_nwkEdCtx.parentCandidateIeee, parentIeeeAddr);
	} else {
		ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.parentCandidateIeee);
	}
}

void tl_zbNwkEdMinimalParentCandidateClear(void)
{
	g_nwkEdCtx.parentCandidateValid = FALSE;
	g_nwkEdCtx.parentCandidateShortAddr = MAC_SHORT_ADDR_NONE;
	ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.parentCandidateIeee);
}

bool tl_zbNwkEdMinimalParentCandidateGet(u16 *parentShortAddr, addrExt_t parentIeeeAddr)
{
	if (!g_nwkEdCtx.parentCandidateValid) {
		return FALSE;
	}

	if (parentShortAddr != NULL) {
		*parentShortAddr = g_nwkEdCtx.parentCandidateShortAddr;
	}
	if (parentIeeeAddr != NULL) {
		ZB_IEEE_ADDR_COPY(parentIeeeAddr, g_nwkEdCtx.parentCandidateIeee);
	}

	return TRUE;
}

void tl_zbNwkEdMinimalSetFixedJoinTarget(u8 channel, u16 panId, u16 shortAddr,
					 const u8 *extPanId, const u8 *nwkKey, const u8 *tcAddr)
{
	g_nwkEdCtx.fixedJoinValid = TRUE;
	g_nwkEdCtx.fixedJoinChannel = channel;
	g_nwkEdCtx.fixedJoinPanId = panId;
	g_nwkEdCtx.fixedJoinShortAddr = shortAddr;
	if (extPanId != NULL) {
		ZB_EXTPANID_COPY(g_nwkEdCtx.fixedJoinExtPanId, extPanId);
	} else {
		memset(g_nwkEdCtx.fixedJoinExtPanId, 0, sizeof(g_nwkEdCtx.fixedJoinExtPanId));
	}
	if (nwkKey != NULL) {
		memcpy(g_nwkEdCtx.fixedJoinNwkKey, nwkKey, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	} else {
		memset(g_nwkEdCtx.fixedJoinNwkKey, 0, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	}
	if (tcAddr != NULL) {
		ZB_IEEE_ADDR_COPY(g_nwkEdCtx.fixedJoinTcAddr, tcAddr);
	} else {
		ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.fixedJoinTcAddr);
	}

	if (g_zbNwkCtx.joined && panId != MAC_INVALID_PANID &&
	    shortAddr != MAC_SHORT_ADDR_NONE) {
		g_nwkEdCtx.activeChannel = channel;
		g_nwkEdCtx.activePanId = panId;
		g_nwkEdCtx.activeShortAddr = g_zbMacPib.shortAddress;
		g_nwkEdCtx.activeParentShortAddr = shortAddr;
		if (extPanId != NULL) {
			ZB_EXTPANID_COPY(g_nwkEdCtx.activeExtPanId, extPanId);
		}
	}
}

bool tl_zbNwkEdMinimalGetFixedJoinTarget(u8 *channel, u16 *panId, u16 *shortAddr,
					 extPANId_t extPanId, u8 *nwkKey, addrExt_t tcAddr)
{
	if (!g_nwkEdCtx.fixedJoinValid) {
		return FALSE;
	}

	if (channel != NULL) {
		*channel = g_nwkEdCtx.fixedJoinChannel;
	}
	if (panId != NULL) {
		*panId = g_nwkEdCtx.fixedJoinPanId;
	}
	if (shortAddr != NULL) {
		*shortAddr = g_nwkEdCtx.fixedJoinShortAddr;
	}
	if (extPanId != NULL) {
		ZB_EXTPANID_COPY(extPanId, g_nwkEdCtx.fixedJoinExtPanId);
	}
	if (nwkKey != NULL) {
		memcpy(nwkKey, g_nwkEdCtx.fixedJoinNwkKey, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	}
	if (tcAddr != NULL) {
		ZB_IEEE_ADDR_COPY(tcAddr, g_nwkEdCtx.fixedJoinTcAddr);
	}

	return TRUE;
}

void tl_zbNwkInit(u8 coldReset)
{
	nwk_ed_minimal_reset(coldReset ? FALSE : TRUE);
}

void tl_zbNwkNlmeResetRequestHandler(void *arg)
{
	nlme_reset_req_t *pReq = (nlme_reset_req_t *)arg;
	nlme_reset_cnf_t cnf = {.status = NWK_STATUS_SUCCESS};

	nwk_ed_minimal_reset((pReq != NULL) ? pReq->warmStart : FALSE);

	if (zdoAppIndCbLst != NULL && zdoAppIndCbLst->zdpResetCnfCb != NULL) {
		zdoAppIndCbLst->zdpResetCnfCb(&cnf);
	}
}

void tl_zbNwkTaskProc(void)
{
}

static void nwk_ed_minimal_handle_beacon_event(const nwk_ed_minimal_rx_evt_t *evt)
{
	struct zb_platform_bdb_join_profile profile;
	bool have_profile;

	if (evt == NULL || g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_DISCOVERY) {
		return;
	}

	have_profile = nwk_ed_minimal_get_join_profile(&profile);
	if (have_profile && !nwk_ed_minimal_beacon_matches_join_profile(evt, &profile)) {
		return;
	}

	if (!g_nwkEdCtx.haveBeaconCandidate || (evt->rssi > g_nwkEdCtx.bestBeaconRssi)) {
		zb_nwk_ed_trace[10] = ((u32)evt->beacon.panId << 16) |
				       evt->beacon.parentShortAddr;
		g_nwkEdCtx.haveBeaconCandidate = TRUE;
		g_nwkEdCtx.bestBeaconRssi = evt->rssi;
		g_nwkEdCtx.candidatePanId = evt->beacon.panId;
		g_nwkEdCtx.candidateShortAddr = evt->beacon.parentShortAddr;
		ZB_EXTPANID_COPY(g_nwkEdCtx.candidateExtPanId, evt->beacon.extPanId);
		tl_zbNwkEdMinimalParentCandidateSet(evt->beacon.parentShortAddr, NULL);
		tl_zbNwkEdMinimalSetFixedJoinTarget(evt->beacon.channel, evt->beacon.panId,
						    evt->beacon.parentShortAddr, evt->beacon.extPanId,
						    have_profile && profile.network_key_valid ? profile.network_key : NULL,
						    have_profile && profile.tc_addr_valid ? profile.tc_addr : NULL);
		if (have_profile) {
			LOG_INF("matched beacon candidate: pan 0x%04x parent 0x%04x ch %u rssi %d",
				evt->beacon.panId, evt->beacon.parentShortAddr, evt->beacon.channel,
				evt->rssi);
		} else {
			LOG_INF("beacon candidate: pan 0x%04x parent 0x%04x ch %u rssi %d",
				evt->beacon.panId, evt->beacon.parentShortAddr, evt->beacon.channel,
				evt->rssi);
		}
	}
}

static void nwk_ed_minimal_handle_traffic_candidate_event(const nwk_ed_minimal_rx_evt_t *evt)
{
	bool preferNewCandidate;

	if (evt == NULL || g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_DISCOVERY) {
		return;
	}

	if (g_nwkEdCtx.haveBeaconCandidate) {
		return;
	}

	preferNewCandidate = !g_nwkEdCtx.parentCandidateValid;
	if (!preferNewCandidate &&
	    g_nwkEdCtx.candidateShortAddr != MAC_SHORT_ADDR_BROADCAST &&
	    evt->traffic.parentShortAddr == 0x0000U) {
		preferNewCandidate = TRUE;
	}
	if (!preferNewCandidate && evt->rssi > g_nwkEdCtx.bestBeaconRssi) {
		preferNewCandidate = TRUE;
	}

	if (preferNewCandidate) {
		zb_nwk_ed_trace[9]++;
		zb_nwk_ed_trace[10] = ((u32)evt->traffic.panId << 16) |
				       evt->traffic.parentShortAddr;
		g_nwkEdCtx.bestBeaconRssi = evt->rssi;
		g_nwkEdCtx.candidatePanId = evt->traffic.panId;
		g_nwkEdCtx.candidateShortAddr = evt->traffic.parentShortAddr;
		memset(g_nwkEdCtx.candidateExtPanId, 0, sizeof(g_nwkEdCtx.candidateExtPanId));
		tl_zbNwkEdMinimalParentCandidateSet(evt->traffic.parentShortAddr, NULL);
		tl_zbNwkEdMinimalSetFixedJoinTarget(evt->traffic.channel, evt->traffic.panId,
						    evt->traffic.parentShortAddr, NULL,
						    NULL, NULL);
		LOG_INF("traffic candidate: pan 0x%04x parent 0x%04x ch %u rssi %d",
			evt->traffic.panId, evt->traffic.parentShortAddr,
			evt->traffic.channel, evt->rssi);
	}
}

static void nwk_ed_minimal_handle_assoc_rsp_event(const nwk_ed_minimal_rx_evt_t *evt)
{
	u8 zdoStatus;
	bool rejoinMode;

	if (evt == NULL) {
		return;
	}
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_JOINING &&
	    g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_REJOIN) {
		return;
	}

	rejoinMode = (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN);
	if (evt->assocRsp.srcShortValid &&
	    evt->assocRsp.srcShortAddr != g_nwkEdCtx.activeParentShortAddr) {
		return;
	}
	if (evt->assocRsp.dstExtValid &&
	    memcmp(evt->assocRsp.dstExtAddr, g_zbMacPib.extAddress, sizeof(addrExt_t)) != 0) {
		return;
	}
	if (evt->assocRsp.dstShortValid &&
	    evt->assocRsp.dstShortAddr != MAC_SHORT_ADDR_BROADCAST &&
	    evt->assocRsp.dstShortAddr != g_zbMacPib.shortAddress) {
		return;
	}

	if (evt->assocRsp.macStatus == MAC_SUCCESS) {
		if (evt->assocRsp.srcExtValid) {
			tl_zbNwkEdMinimalParentCandidateSet(g_nwkEdCtx.activeParentShortAddr,
							      evt->assocRsp.srcExtAddr);
			ZB_IEEE_ADDR_COPY(g_zbMacPib.coordExtAddress, evt->assocRsp.srcExtAddr);
		}
		nwk_ed_minimal_apply_tc_context();

		g_zbMacPib.panId = g_nwkEdCtx.activePanId;
		g_zbMacPib.shortAddress = evt->assocRsp.assignedShortAddr;
		g_zbMacPib.coordShortAddress = g_nwkEdCtx.activeParentShortAddr;
		g_zbMacPib.associatedPanCoord = TRUE;
		g_nwkEdCtx.activeShortAddr = evt->assocRsp.assignedShortAddr;
		zb_radio_port_update_filters(g_nwkEdCtx.activePanId,
					     evt->assocRsp.assignedShortAddr,
					     g_zbMacPib.extAddress);

		g_zbNIB.panId = g_nwkEdCtx.activePanId;
		g_zbNIB.nwkAddr = evt->assocRsp.assignedShortAddr;
		g_zbNIB.depth = 1U;
		ZB_EXTPANID_COPY(g_zbNIB.extPANId, g_nwkEdCtx.activeExtPanId);
		nwk_ed_minimal_install_fixed_join_key_if_needed();
		nwk_ed_minimal_sync_zb_info_from_runtime();

		if (!aps_ib.aps_authenticated) {
			LOG_INF("%s associated: short 0x%04x pan 0x%04x parent 0x%04x, waiting transport key",
				rejoinMode ? "rejoin" : "join", evt->assocRsp.assignedShortAddr,
				g_nwkEdCtx.activePanId, g_nwkEdCtx.activeParentShortAddr);
			nwk_ed_minimal_enter_interview(rejoinMode);
			return;
		}

		nwk_ed_minimal_complete_join(rejoinMode);
		return;
	}

	switch (evt->assocRsp.macStatus) {
	case MAC_STA_PAN_AT_CAPACITY:
		zdoStatus = ZDO_TABLE_FULL;
		break;
	case MAC_STA_PAN_ACCESS_DENIED:
		zdoStatus = ZDO_NOT_PERMITTED;
		break;
	default:
		zdoStatus = ZDO_NOT_SUPPORTED;
		break;
	}

	LOG_WRN("%s association rejected: mac status 0x%02x",
		rejoinMode ? "rejoin" : "join", evt->assocRsp.macStatus);
	nwk_ed_minimal_finish_join(zdoStatus, rejoinMode);
}

static void nwk_ed_minimal_rx_event_task(void *arg)
{
	nwk_ed_minimal_rx_evt_t evt;

	ARG_UNUSED(arg);
	while (nwk_ed_minimal_rx_evt_pop(&evt)) {
		switch (evt.type) {
		case NWK_ED_MINIMAL_RX_EVT_BEACON:
			nwk_ed_minimal_handle_beacon_event(&evt);
			break;
		case NWK_ED_MINIMAL_RX_EVT_TRAFFIC_CANDIDATE:
			nwk_ed_minimal_handle_traffic_candidate_event(&evt);
			break;
		case NWK_ED_MINIMAL_RX_EVT_ASSOC_RSP:
			nwk_ed_minimal_handle_assoc_rsp_event(&evt);
			break;
		default:
			break;
		}
	}
}

void tl_zbNwkEdMinimalMacRxIndicate(const u8 *macPld, u8 len, s8 rssi)
{
	nwk_ed_minimal_mac_hdr_t hdr;
	u8 payloadLen;
	nwk_ed_minimal_rx_evt_t evt;
	bool discoveryActive;
	bool joinActive;

	if (macPld == NULL || len < (MAC_MIN_HDR_LEN + 2U)) {
		return;
	}

	zb_nwk_ed_trace[6]++;
	discoveryActive = (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_DISCOVERY);
	joinActive = (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_JOINING) ||
		     (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN);

	/* Incoming PSDU includes FCS bytes at the end. */
	payloadLen = (u8)(len - 2U);
	if (!nwk_ed_minimal_parse_mac_header(macPld, payloadLen, &hdr)) {
		zb_nwk_ed_trace[7] = ((u32)0xffU << 24) | payloadLen;
		return;
	}
	zb_nwk_ed_trace[7] = ((u32)hdr.frameType << 24) | ((u32)hdr.fcf << 8) |
			      rf_getChannel();

	if (hdr.frameType == MAC_FRAME_BEACON) {
		u16 panId = MAC_INVALID_PANID;
		u16 parentShortAddr = MAC_SHORT_ADDR_NONE;
		extPANId_t extPanId = {0};

		if (!discoveryActive) {
			return;
		}

		if (!nwk_ed_minimal_parse_beacon_candidate(macPld, payloadLen, &hdr, &panId,
							   &parentShortAddr, extPanId)) {
			return;
		}

		zb_nwk_ed_trace[8]++;
		memset(&evt, 0, sizeof(evt));
		evt.type = NWK_ED_MINIMAL_RX_EVT_BEACON;
		evt.rssi = rssi;
		evt.beacon.panId = panId;
		evt.beacon.parentShortAddr = parentShortAddr;
		evt.beacon.channel = rf_getChannel();
		ZB_EXTPANID_COPY(evt.beacon.extPanId, extPanId);
		if (nwk_ed_minimal_rx_evt_push(&evt)) {
			(void)TL_SCHEDULE_TASK(nwk_ed_minimal_rx_event_task, NULL);
		} else {
		}
		return;
	}

	if (hdr.frameType == MAC_FRAME_DATA && hdr.srcPanValid && hdr.srcShortValid &&
	    hdr.dstShortValid && hdr.dstShortAddr == MAC_SHORT_ADDR_BROADCAST) {
		if (!discoveryActive) {
			return;
		}

		memset(&evt, 0, sizeof(evt));
		evt.type = NWK_ED_MINIMAL_RX_EVT_TRAFFIC_CANDIDATE;
		evt.rssi = rssi;
		evt.traffic.panId = hdr.srcPanId;
		evt.traffic.parentShortAddr = hdr.srcShortAddr;
		evt.traffic.channel = rf_getChannel();
		if (nwk_ed_minimal_rx_evt_push(&evt)) {
			(void)TL_SCHEDULE_TASK(nwk_ed_minimal_rx_event_task, NULL);
		}
		return;
	}

	if (hdr.frameType == MAC_FRAME_COMMAND) {
		u8 cmdIdx = hdr.headerLen;
		u8 cmdId;
		u8 assocStatus;

		if (cmdIdx + 4U > payloadLen) {
			return;
		}
		cmdId = macPld[cmdIdx];
		if (cmdId == MAC_CMD_DATA_REQUEST &&
		    hdr.srcPanValid &&
		    hdr.dstShortValid &&
		    hdr.dstShortAddr != MAC_SHORT_ADDR_BROADCAST) {
			if (!discoveryActive) {
				return;
			}
			memset(&evt, 0, sizeof(evt));
			evt.type = NWK_ED_MINIMAL_RX_EVT_TRAFFIC_CANDIDATE;
			evt.rssi = rssi;
			evt.traffic.panId = hdr.srcPanId;
			evt.traffic.parentShortAddr = hdr.dstShortAddr;
			evt.traffic.channel = rf_getChannel();
			if (nwk_ed_minimal_rx_evt_push(&evt)) {
				(void)TL_SCHEDULE_TASK(nwk_ed_minimal_rx_event_task, NULL);
			}
			return;
		}
		if (cmdId != MAC_CMD_ASSOCIATION_RESPONSE) {
			return;
		}
		if (!joinActive) {
			return;
		}
		memset(&evt, 0, sizeof(evt));
		evt.type = NWK_ED_MINIMAL_RX_EVT_ASSOC_RSP;
		assocStatus = macPld[cmdIdx + 3U];
		evt.assocRsp.macStatus = assocStatus;
		evt.assocRsp.assignedShortAddr = nwk_ed_minimal_u16_from_le(&macPld[cmdIdx + 1U]);
		evt.assocRsp.srcExtValid = hdr.srcExtValid;
		evt.assocRsp.srcShortValid = hdr.srcShortValid;
		evt.assocRsp.srcShortAddr = hdr.srcShortAddr;
		evt.assocRsp.dstExtValid = hdr.dstExtValid;
		evt.assocRsp.dstShortValid = hdr.dstShortValid;
		evt.assocRsp.dstShortAddr = hdr.dstShortAddr;
		if (hdr.srcExtValid) {
			ZB_IEEE_ADDR_COPY(evt.assocRsp.srcExtAddr, hdr.srcExtAddr);
		}
		if (hdr.dstExtValid) {
			ZB_IEEE_ADDR_COPY(evt.assocRsp.dstExtAddr, hdr.dstExtAddr);
		}
		if (nwk_ed_minimal_rx_evt_push(&evt)) {
			(void)TL_SCHEDULE_TASK(nwk_ed_minimal_rx_event_task, NULL);
		}
	}
}
