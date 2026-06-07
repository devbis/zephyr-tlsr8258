/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"
#include "mac/includes/mac_phy.h"
#include "nwk_schedule_fallback.h"
#include "os/ev_timer.h"

#include <errno.h>
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
#define NWK_ED_MINIMAL_BEACON_EBUSY_RETRY_MS    20U
#define NWK_ED_MINIMAL_BEACON_EBUSY_MAX_RETRY    3U
#define NWK_ED_MINIMAL_ASSOC_EBUSY_RETRY_MS     20U
#define NWK_ED_MINIMAL_ASSOC_EBUSY_MAX_RETRY    3U
#define NWK_ED_MINIMAL_RX_EVT_Q_LEN       8U
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
	bool fixedJoinUsesPreconfiguredNwkKey;
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
	bool interviewKickScheduled;
	u8 interviewPollCount;
	u32 interviewPollIntervalMs;
	bool endDevTimeoutRspSeen;
	bool endDevTimeoutReqScheduled;
	u32 rxEvtDropCount;
	u32 rxEvtOverflowCount;
	nwk_ed_minimal_rx_evt_type_t lastRxEvtDropType;
	u8 beaconEbusyRetry;
	u8 assocEbusyRetry;

	ev_timer_event_t stateTimer;
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
extern volatile u32 zb_post_tk_trace[16];
volatile u32 zb_nwk_beacon_frame_count;
volatile u32 zb_nwk_beacon_parse_fail_count;
volatile u32 zb_nwk_beacon_discovery_inactive_count;
volatile u32 zb_nwk_beacon_success_count;
volatile u32 zb_nwk_beacon_last_len;
volatile u32 zb_nwk_beacon_last_fcf;
volatile u32 zb_nwk_ed_interview_trace[4];
volatile u32 zb_nwk_ed_poll_trace[8] = {0x4e504f4cU};
#if defined(CONFIG_ZIGBEE_DEBUG_TRACES)
volatile u32 zb_nwk_ed_timer_trace[64] = {0xb7e50000U};
static u8 zb_nwk_ed_timer_trace_pos;
#endif

static void nwk_ed_minimal_joined_idle_poll_schedule(u32 timeoutMs);
static void nwk_ed_minimal_timeout_req_schedule(u32 timeoutMs);
static void nwk_ed_minimal_transport_key_done_task(void *arg);

#if defined(CONFIG_ZIGBEE_DEBUG_TRACES)
static void nwk_ed_minimal_timer_trace_put(u32 tag)
{
	u8 span = (u8)(ARRAY_SIZE(zb_nwk_ed_timer_trace) - 2U);
	u8 slot;
	u8 prev_slot;

	if (zb_nwk_ed_timer_trace_pos != 0U) {
		prev_slot = (u8)(2U + ((zb_nwk_ed_timer_trace_pos - 1U) % span));
		if (zb_nwk_ed_timer_trace[prev_slot] == tag) {
			return;
		}
	}

	slot = (u8)(2U + (zb_nwk_ed_timer_trace_pos % span));
	zb_nwk_ed_timer_trace[slot] = tag;
	zb_nwk_ed_timer_trace_pos++;
	zb_nwk_ed_timer_trace[1] = zb_nwk_ed_timer_trace_pos;
}
#else
static void nwk_ed_minimal_timer_trace_put(u32 tag)
{
	ARG_UNUSED(tag);
}
#endif

extern void tl_zdoEdMinimalDiscoveryDone(u8 status);
extern void tl_zdoEdMinimalAssocDone(u8 status, bool rejoinMode);
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
	nwk_ed_minimal_timer_trace_put((0x07U << 24) |
				       ((u32)g_nwkEdCtx.state << 16) |
				       g_nwkEdCtx.assocPollCount);
	ev_unon_timer(&g_nwkEdCtx.stateTimer);
}

static void nwk_ed_minimal_timeout_req_cancel(void)
{
	ev_unon_timer(&g_nwkEdCtx.timeoutReqTimer);
}

static void nwk_ed_minimal_timer_start(u32 timeoutMs);
static void nwk_ed_minimal_joined_idle_poll_restart(u32 timeoutMs);
static void nwk_ed_minimal_post_join_poll_task(void *arg);
static void nwk_ed_minimal_post_join_announce_task(void *arg);
static int nwk_ed_minimal_post_join_poll_timer(void *arg);
static void nwk_ed_minimal_interview_kick_task(void *arg);
static void nwk_ed_minimal_timeout_req_task(void *arg);
static void nwk_ed_minimal_rx_event_task(void *arg);
static void nwk_ed_minimal_repair_joined_context_if_needed(void);
static bool nwk_ed_minimal_send_data_request(void);
static bool nwk_ed_minimal_send_rejoin_request(void);
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
	} else if (!ZB_IEEE_ADDR_IS_ZERO(g_zbMacPib.coordExtAddress) &&
		   !ZB_IEEE_ADDR_IS_INVALID(g_zbMacPib.coordExtAddress)) {
		tcAddr = g_zbMacPib.coordExtAddress;
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

static bool nwk_ed_minimal_fixed_join_target_locked(void)
{
	return g_nwkEdCtx.fixedJoinValid &&
	       (g_nwkEdCtx.fixedJoinUsesPreconfiguredNwkKey ||
		(!ZB_IEEE_ADDR_IS_ZERO(g_nwkEdCtx.fixedJoinTcAddr) &&
		 !ZB_IEEE_ADDR_IS_INVALID(g_nwkEdCtx.fixedJoinTcAddr)));
}

static bool nwk_ed_minimal_parse_beacon_candidate(const u8 *psdu, u8 len,
						  const nwk_ed_minimal_mac_hdr_t *hdr,
						  u16 *panId, u16 *coordShortAddr, extPANId_t extPanId,
						  bool *associationPermit)
{
	u8 idx;
	u8 gtsSpec;
	u8 pendingSpec;
	u8 gtsDescCount;
	u8 pendingShortCount;
	u8 pendingExtCount;
	u8 superframeSpec2;

	if (psdu == NULL || hdr == NULL || panId == NULL || coordShortAddr == NULL ||
	    extPanId == NULL || associationPermit == NULL ||
	    !hdr->srcPanValid || !hdr->srcShortValid) {
		return FALSE;
	}

	idx = hdr->headerLen;
	if (idx + 3U > len) {
		return FALSE;
	}

	superframeSpec2 = psdu[idx + 1U];
	if ((superframeSpec2 & 0x7FU) == 0U) {
		return FALSE;
	}
	*associationPermit = (superframeSpec2 & BIT(7)) != 0U;

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

static void nwk_ed_minimal_mark_preconfigured_join_secure(void)
{
	ss_ib.preConfiguredKeyType |= SS_PRECONFIGURED_NWKKEY;
	if (ss_ib.securityLevel == 0U) {
		ss_ib.securityLevel = 5U;
	}
	aps_ib.aps_authenticated = 1U;
	aps_ib.aps_use_insecure_join = FALSE;
}

static bool nwk_ed_minimal_install_fixed_join_key_if_needed(void)
{
	ss_material_set_t *material;
	u8 *active_key = nwk_ed_minimal_active_nwk_key_get();

	if (!g_nwkEdCtx.fixedJoinUsesPreconfiguredNwkKey ||
	    !nwk_ed_minimal_key_is_set(g_nwkEdCtx.fixedJoinNwkKey)) {
		return FALSE;
	}

	if (!nwk_ed_minimal_key_is_set(active_key)) {
		material = &ss_ib.nwkSecurMaterialSet[0];
		memcpy(material->key, g_nwkEdCtx.fixedJoinNwkKey, SEC_KEY_LEN);
		material->keySeqNum = 0U;
		material->keyType = 1U;
		ss_ib.activeSecureMaterialIndex = 0U;
		ss_ib.activeKeySeqNum = 0U;
		nwk_ed_minimal_mark_preconfigured_join_secure();
		return TRUE;
	}

	if (memcmp(active_key, g_nwkEdCtx.fixedJoinNwkKey, SEC_KEY_LEN) != 0) {
		return FALSE;
	}
	if ((ss_ib.preConfiguredKeyType & SS_PRECONFIGURED_NWKKEY) == 0U) {
		return FALSE;
	}

	nwk_ed_minimal_mark_preconfigured_join_secure();
	return TRUE;
}

static u8 nwk_ed_minimal_end_device_initiator_bit(void)
{
	return (g_zbNIB.parentInfo > 0U) ? (u8)(g_zbNIB.parentInfo - 1U) : 0U;
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
	if (nwk_ed_minimal_end_device_initiator_bit() != 0U) {
		nwkFcf |= BIT(13);
	}

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
	/* Frame counter NOT persisted here — synchronous NVS write inside the
	 * TX path can stall the Zigbee thread on TLSR8258 flash GC, breaking
	 * interview/TCLK timing.  Counter is persisted on join completion. */

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
	g_nwkEdCtx.beaconEbusyRetry = 0U;
	g_nwkEdCtx.assocEbusyRetry = 0U;

	if (status != ZDO_SUCCESS && !rejoinMode) {
		g_zbNwkCtx.joined = 0;
	}

	tl_zdoEdMinimalJoinDone(status, rejoinMode);
}

static int nwk_ed_minimal_deferred_zb_info_save_timer(void *arg)
{
	ARG_UNUSED(arg);
	zb_info_save(NULL);
	return -1;
}

static void nwk_ed_minimal_complete_join(bool rejoinMode)
{
	zb_post_tk_trace[5] = 0x50000000U | (zb_post_tk_trace[5] + 1U);
	g_zbNwkCtx.joined = 1U;
	g_zbNwkCtx.is_factory_new = 0U;
	g_zbNwkCtx.parentIsChanged = 0U;
	g_zbNwkCtx.state = NLME_STATE_IDLE;
	g_zbNwkCtx.user_state = NLME_IDLE;
	/*
	 * Defer zb_info_save() ~15s after join.  Empirically: vendor's
	 * tl_zbTaskPost pattern (which runs the save as the next zb-thread
	 * task tick) still stalls the device hard enough on our Zephyr NVS
	 * backend that the post-join polling stops mid-interview and Z2M
	 * marks the device FAILED.  Capture analysis shows the device
	 * receives the APS Transport-Key, then disappears off the air: the
	 * task that runs zb_info_save chains nv_nwkFrameCountSaveToFlash +
	 * zdo_ssInfoSaveToFlash + the blob write, and on TLSR8258+Zephyr
	 * NVS that combination can hang the zb thread (see the earlier
	 * fix that removed nv_nwkFrameCountSaveToFlash from the TX hot
	 * path).  Postponing the save out to a long timer lets the
	 * Node_Desc_req / Active_EP_req / SimpleDesc_req exchange complete
	 * first; the persistence will land before the device is power-cycled,
	 * which is the only situation where saved state actually matters.
	 */
	(void)TL_ZB_TIMER_SCHEDULE(nwk_ed_minimal_deferred_zb_info_save_timer,
				   NULL, 15000U);

	LOG_INF("%s complete: short 0x%04x pan 0x%04x parent 0x%04x",
		rejoinMode ? "rejoin" : "join", g_zbNIB.nwkAddr, g_zbNIB.panId,
		g_zbMacPib.coordShortAddress);
	nwk_ed_minimal_finish_join(ZDO_SUCCESS, rejoinMode);
	zb_post_tk_trace[6] = 0x60000000U | (zb_post_tk_trace[6] + 1U);
	memset(&g_nwkEdCtx.opTimer, 0, sizeof(g_nwkEdCtx.opTimer));
	if (!zb_nwk_schedule_task_or_timer(nwk_ed_minimal_post_join_poll_task,
					       &g_nwkEdCtx.opTimer,
					       nwk_ed_minimal_post_join_poll_timer,
					       NULL, 1U)) {
		LOG_ERR("%s complete: failed to schedule post-join poll task",
			rejoinMode ? "rejoin" : "join");
	}

	if (!rejoinMode &&
	    TL_SCHEDULE_TASK(nwk_ed_minimal_post_join_announce_task, NULL) != RET_OK) {
		LOG_ERR("join complete: failed to schedule post-join announce task");
	}
}

static void nwk_ed_minimal_enter_interview(bool rejoinMode)
{
	nwk_ed_minimal_timer_cancel();
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_INTERVIEW;
	g_nwkEdCtx.interviewRejoinMode = rejoinMode;
	g_nwkEdCtx.assocPollCount = 0U;
	g_nwkEdCtx.interviewKickScheduled = FALSE;
	zb_nwk_ed_interview_trace[0] = 0xb7e10000U | ((u32)rejoinMode << 8) |
				       g_nwkEdCtx.state;

	if (TL_SCHEDULE_TASK(nwk_ed_minimal_interview_kick_task, NULL) == RET_OK) {
		g_nwkEdCtx.interviewKickScheduled = TRUE;
		return;
	}

	nwk_ed_minimal_timer_start(NWK_ED_MINIMAL_INTERVIEW_POLL_MS);
}

static bool nwk_ed_minimal_send_data_request(void)
{
	u8 frame[MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN + MAC_PAN_ID_FIELD_LEN +
		 MAC_SHORT_ADDR_FIELD_LEN + MAC_EXT_ADDR_FIELD_LEN + 1U];
	u8 idx = 0;
	u16 fcf = 0;
	bool useShortSrc = g_zbNIB.nwkAddr < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED;
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
	zb_nwk_ed_poll_trace[1]++;
	zb_nwk_ed_poll_trace[2] = ((u32)g_nwkEdCtx.state << 24) |
				  ((u32)(useShortSrc ? 1U : 0U) << 16) |
				  g_nwkEdCtx.activeChannel;
	zb_nwk_ed_poll_trace[3] = ((u32)g_nwkEdCtx.activeParentShortAddr << 16) |
				  g_zbNIB.nwkAddr;
	zb_nwk_ed_poll_trace[4] = ((u32)fcf << 16) | idx;

	rc = zb_platform_radio_send_raw_psdu(frame, idx);
	zb_nwk_ed_poll_trace[5] = ((u32)(u8)rc << 24) |
				  ((u32)frame[MAC_FCF_FIELD_LEN] << 16) |
				  frame[idx - 1U];
	nwk_ed_minimal_timer_trace_put((0x05U << 24) |
				       ((u32)g_nwkEdCtx.state << 16) |
				       ((u32)(u8)idx << 8) |
				       (u16)rc);
	zb_nwk_ed_trace[14]++;
	zb_nwk_ed_trace[15] = ((u32)(u8)idx << 24) | ((u32)MAC_CMD_DATA_REQUEST << 16) |
			       (u16)rc;
	if (rc < 0) {
		LOG_WRN("data request tx failed (rc=%d len=%u)", rc, idx);
		return FALSE;
	}
	return TRUE;
}

static bool nwk_ed_minimal_send_rejoin_request(void)
{
	u8 frame[127];
	u8 nonce[13];
	u8 *nwk_key;
	size_t idx = 0U;
	size_t nwk_hdr_idx;
	size_t payload_idx;
	size_t nwk_hdr_len;
	u16 mac_fcf = 0U;
	u16 nwk_fcf = 0U;
	u8 capability = 0U;
	u8 enc_len;
	bool secure;
	u8 sec_ctrl_idx = 0U;
	u32 nwk_frame_counter = 0U;
	int rc;

	if (g_nwkEdCtx.activePanId == MAC_INVALID_PANID ||
	    g_nwkEdCtx.activeParentShortAddr >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED ||
	    g_nwkEdCtx.activeShortAddr >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED) {
		return FALSE;
	}

	nwk_ed_minimal_install_fixed_join_key_if_needed();
	nwk_key = nwk_ed_minimal_active_nwk_key_get();
	secure = !aps_ib.aps_use_insecure_join &&
		 (ss_ib.securityLevel != 0U) &&
		 nwk_ed_minimal_key_is_set(nwk_key);

	nwk_ed_minimal_channel_set(g_nwkEdCtx.activeChannel);
	zb_radio_port_update_filters(g_nwkEdCtx.activePanId, g_nwkEdCtx.activeShortAddr,
				     g_zbMacPib.extAddress);

	mac_fcf |= MAC_FRAME_DATA;
	mac_fcf |= MAC_FCF_ACK_REQ_BIT;
	mac_fcf |= MAC_FCF_INTRA_PAN_MASK;
	mac_fcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_DST_ADDR_MODE_POS;
	mac_fcf |= (u16)ZB_ADDR_16BIT_DEV_OR_BROADCAST << MAC_FCF_SRC_ADDR_MODE_POS;

	COPY_U16TOBUFFER(&frame[idx], mac_fcf);
	idx += MAC_FCF_FIELD_LEN;
	frame[idx++] = ZB_MAC_DSN();
	ZB_INC_MAC_DSN();
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activePanId);
	idx += MAC_PAN_ID_FIELD_LEN;
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activeParentShortAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activeShortAddr);
	idx += MAC_SHORT_ADDR_FIELD_LEN;

	nwk_hdr_idx = idx;
	nwk_fcf |= FRAME_TYPE_COMMAND;
	nwk_fcf |= (u16)(0x02U << 2);
	if (secure) {
		nwk_fcf |= BIT(9);
	}
	nwk_fcf |= BIT(12);
	if (nwk_ed_minimal_end_device_initiator_bit() != 0U) {
		nwk_fcf |= BIT(13);
	}

	COPY_U16TOBUFFER(&frame[idx], nwk_fcf);
	idx += 2U;
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activeParentShortAddr);
	idx += 2U;
	COPY_U16TOBUFFER(&frame[idx], g_nwkEdCtx.activeShortAddr);
	idx += 2U;
	frame[idx++] = 1U;
	frame[idx++] = g_zbNIB.seqNum++;
	memcpy(&frame[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
	idx += sizeof(addrExt_t);

	if (secure) {
		sec_ctrl_idx = (u8)idx;

		frame[idx++] = NWK_ED_MINIMAL_NWK_SEC_CTRL;
		COPY_U32TOBUFFER(&frame[idx], ss_ib.outgoingFrameCounter);
		nwk_frame_counter = ss_ib.outgoingFrameCounter++;
		idx += 4U;
		memcpy(&frame[idx], g_zbMacPib.extAddress, sizeof(addrExt_t));
		idx += sizeof(addrExt_t);
		frame[idx++] = ss_ib.activeKeySeqNum;
	}

	payload_idx = idx;
	frame[idx++] = NWK_CMD_REJOIN_REQUEST;
	capability |= g_zbNIB.capabilityInfo.altPanCoord ? BIT(0) : 0U;
	capability |= g_zbNIB.capabilityInfo.devType ? BIT(1) : 0U;
	capability |= g_zbNIB.capabilityInfo.powerSrc ? BIT(2) : 0U;
	capability |= g_zbMacPib.rxOnWhenIdle ? BIT(3) : 0U;
	capability |= g_zbNIB.capabilityInfo.secuCapability ? BIT(6) : 0U;
	capability |= BIT(7);
	if (g_zbNIB.capabilityInfo.devType) {
		capability &= ~BIT(7);
	}
	frame[idx++] = capability;

	if (secure) {
		nwk_hdr_len = payload_idx - nwk_hdr_idx;
		memcpy(nonce, g_zbMacPib.extAddress, sizeof(addrExt_t));
		COPY_U32TOBUFFER(&nonce[8], nwk_frame_counter);
		nonce[12] = NWK_ED_MINIMAL_NWK_SEC_CTRL;
		enc_len = zb_minimal_ccm_encrypt_auth(nwk_key, nonce, NWK_ED_MINIMAL_NWK_MIC_LEN,
						      &frame[nwk_hdr_idx], (u8)nwk_hdr_len,
						      &frame[payload_idx], 2U,
						      &frame[payload_idx + 2U]);
		if (enc_len != (u8)(2U + NWK_ED_MINIMAL_NWK_MIC_LEN)) {
			return FALSE;
		}
		frame[sec_ctrl_idx] = NWK_ED_MINIMAL_NWK_SEC_CTRL_WIRE;
		idx = payload_idx + enc_len;
		/* Frame counter persisted on join completion only; see note above. */
	}

	rc = zb_platform_radio_send_raw_psdu(frame, (u8)idx);
	zb_nwk_ed_trace[12] = ((u32)(u8)idx << 24) | ((u32)NWK_CMD_REJOIN_REQUEST << 16) |
			       (u16)rc;
	zb_nwk_ed_trace[13] = ((u32)g_nwkEdCtx.activePanId << 16) |
			       g_nwkEdCtx.activeParentShortAddr;
	if (rc < 0) {
		LOG_WRN("rejoin request tx failed (rc=%d len=%u)", rc, (unsigned)idx);
		return FALSE;
	}

	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_REJOIN;
	g_nwkEdCtx.assocPollCount = 0U;
	nwk_ed_minimal_timer_start(NWK_ED_MINIMAL_JOIN_POLL_MS);
	LOG_INF("rejoin request sent: short 0x%04x pan 0x%04x parent 0x%04x ch %u",
		g_nwkEdCtx.activeShortAddr, g_nwkEdCtx.activePanId,
		g_nwkEdCtx.activeParentShortAddr, g_nwkEdCtx.activeChannel);
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

	if (nwk_ed_minimal_fixed_join_target_locked()) {
		parentShortAddr = g_nwkEdCtx.fixedJoinShortAddr;
		panId = g_nwkEdCtx.fixedJoinPanId;
		ZB_EXTPANID_COPY(extPanId, g_nwkEdCtx.fixedJoinExtPanId);
		channel = g_nwkEdCtx.fixedJoinChannel;
	} else if (g_nwkEdCtx.parentCandidateValid) {
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
	/*
	 * Program the candidate PAN before association so the radio can
	 * acknowledge the long-addressed Association Response on the first join,
	 * matching the vendor MAC flow more closely.
	 *
	 * Hardware validation required: the radio-filter pre-programming effect
	 * on Association Response reception is not exercised by host-only
	 * baselines and must be verified on physical hardware.
	 */
	zb_radio_port_update_filters(panId, MAC_SHORT_ADDR_BROADCAST, g_zbMacPib.extAddress);

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
	if (rc == -EBUSY) {
			LOG_DBG("association request CCA busy ch=%u retry=%u", channel,
				g_nwkEdCtx.assocEbusyRetry);
			g_nwkEdCtx.assocEbusyRetry++;
			g_nwkEdCtx.state = rejoinMode ? NWK_ED_MINIMAL_STATE_REJOIN
					      : NWK_ED_MINIMAL_STATE_JOINING;
			nwk_ed_minimal_timer_start(
				g_nwkEdCtx.assocEbusyRetry <= NWK_ED_MINIMAL_ASSOC_EBUSY_MAX_RETRY
				? NWK_ED_MINIMAL_ASSOC_EBUSY_RETRY_MS
			: NWK_ED_MINIMAL_JOIN_POLL_MS);
		return TRUE;
	} else if (rc < 0) {
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
	if (rc == -EBUSY) {
		LOG_DBG("beacon request CCA busy ch=%u retry=%u",
			g_nwkEdCtx.activeScanChannel, g_nwkEdCtx.beaconEbusyRetry);
		g_nwkEdCtx.beaconEbusyRetry++;
	} else if (rc < 0) {
		g_nwkEdCtx.beaconEbusyRetry = 0U;
		LOG_WRN("beacon request tx failed (rc=%d ch=%u)", rc, g_nwkEdCtx.activeScanChannel);
	} else {
		g_nwkEdCtx.beaconEbusyRetry = 0U;
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
	g_nwkEdCtx.beaconEbusyRetry = 0U;
	nwk_ed_minimal_channel_set(nextChannel);
	nwk_ed_minimal_send_beacon_request();
	nwk_ed_minimal_timer_start(
		(g_nwkEdCtx.beaconEbusyRetry > 0U &&
		 g_nwkEdCtx.beaconEbusyRetry <= NWK_ED_MINIMAL_BEACON_EBUSY_MAX_RETRY)
		? NWK_ED_MINIMAL_BEACON_EBUSY_RETRY_MS
		: nwk_ed_minimal_scan_window_ms(g_nwkEdCtx.lastScanDuration));
	LOG_INF("discovery scanning channel %u", nextChannel);

	return TRUE;
}

static void nwk_ed_minimal_timer_task(void *arg)
{
	ARG_UNUSED(arg);
	nwk_ed_minimal_timer_trace_put((0x03U << 24) |
				       ((u32)g_nwkEdCtx.state << 16) |
				       ((u32)g_nwkEdCtx.assocPollCount << 8) |
				       (u8)(g_nwkEdCtx.interviewKickScheduled ? 1U : 0U));

	/* Drain already queued RX events before making timeout/next-state decisions.
	 * Otherwise a beacon or association response that arrived just before the
	 * timer fires can be ignored until after we already conclude discovery/join
	 * has failed.
	 */
	nwk_ed_minimal_rx_event_task(NULL);

	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_DISCOVERY) {
		/*
		 * If the BeaconReq TX returned -EBUSY (CCA), retry on the same
		 * channel before advancing to the next one.  Skip the retry if we
		 * already have a usable beacon candidate - no need to spam.
		 */
		if (g_nwkEdCtx.beaconEbusyRetry > 0U &&
		    g_nwkEdCtx.beaconEbusyRetry <= NWK_ED_MINIMAL_BEACON_EBUSY_MAX_RETRY &&
		    !g_nwkEdCtx.haveBeaconCandidate &&
		    !g_nwkEdCtx.parentCandidateValid) {
			nwk_ed_minimal_send_beacon_request();
			nwk_ed_minimal_timer_start(
				(g_nwkEdCtx.beaconEbusyRetry > 0U &&
				 g_nwkEdCtx.beaconEbusyRetry <= NWK_ED_MINIMAL_BEACON_EBUSY_MAX_RETRY)
				? NWK_ED_MINIMAL_BEACON_EBUSY_RETRY_MS
				: nwk_ed_minimal_scan_window_ms(
					g_nwkEdCtx.lastScanDuration));
			return;
		}
		g_nwkEdCtx.beaconEbusyRetry = 0U;

		if (g_nwkEdCtx.discoveryForRejoin) {
			if (g_nwkEdCtx.haveBeaconCandidate || g_nwkEdCtx.parentCandidateValid) {
				if (!nwk_ed_minimal_start_assoc(TRUE)) {
					nwk_ed_minimal_finish_join(ZDO_NETWORK_LOST, TRUE);
				}
				return;
			}
		} else if (g_nwkEdCtx.haveBeaconCandidate ||
			   g_nwkEdCtx.parentCandidateValid ||
			   g_nwkEdCtx.fixedJoinValid) {
			/*
			 * Network steering may already have a usable parent/fixed
			 * target from traffic heuristics or bootstrap policy even
			 * when no beacon candidate was recorded. In that case do
			 * not keep the state machine parked in DISCOVERY: proceed
			 * to association immediately.
			 */
			if (!nwk_ed_minimal_start_assoc(FALSE)) {
				nwk_ed_minimal_finish_join(ZDO_TIMEOUT, FALSE);
			}
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

		/*
		 * Retry the AssocReq if the last attempt was rejected by CCA
		 * (-EBUSY).  assocEbusyRetry accumulates across retries so that
		 * the MAX_RETRY ceiling is honoured even if CCA is persistently
		 * busy.  On exhaustion we fall through to the normal data-request
		 * poll which will eventually time out.
		 */
		if (g_nwkEdCtx.assocEbusyRetry > 0U &&
		    g_nwkEdCtx.assocEbusyRetry <= NWK_ED_MINIMAL_ASSOC_EBUSY_MAX_RETRY) {
			if (!nwk_ed_minimal_start_assoc(rejoinMode)) {
				nwk_ed_minimal_finish_join(
					rejoinMode ? ZDO_NETWORK_LOST : ZDO_TIMEOUT,
					rejoinMode);
			}
			return;
		}
		g_nwkEdCtx.assocEbusyRetry = 0U;

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
	u8 rc;

	ARG_UNUSED(arg);

	rc = TL_SCHEDULE_TASK(nwk_ed_minimal_timer_task, NULL);
	nwk_ed_minimal_timer_trace_put((0x02U << 24) |
				       ((u32)g_nwkEdCtx.state << 16) |
				       ((u32)rc << 8) |
				       g_nwkEdCtx.assocPollCount);
	if (rc != RET_OK) {
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
	if (g_nwkEdCtx.stateTimer.cb == NULL) {
		memset(&g_nwkEdCtx.stateTimer, 0, sizeof(g_nwkEdCtx.stateTimer));
		g_nwkEdCtx.stateTimer.cb = nwk_ed_minimal_timer_cb;
	}

	/* Re-arm from a clean state every time.  On hardware the same timer node
	 * can be reused across discovery/join/interview transitions in one
	 * scheduling turn; dropping any stale runtime/list linkage avoids a lost
	 * follow-up expiry.
	 */
	ev_unon_timer(&g_nwkEdCtx.stateTimer);
	nwk_ed_minimal_timer_trace_put((0x01U << 24) |
				       ((u32)g_nwkEdCtx.state << 16) |
				       (timeoutMs & 0xffffU));
	ev_on_timer(&g_nwkEdCtx.stateTimer, timeoutMs);
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
	ev_unon_timer(&g_nwkEdCtx.timeoutReqTimer);
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
	/*
	 * Defer zb_info_save() ~15s — same rationale as
	 * nwk_ed_minimal_complete_join: the flash save chain holds
	 * arch_irq_lock long enough to stall the post-join ZDO interview on
	 * the Zephyr NVS backend.  This repair path runs after a context
	 * mismatch is detected (mostly a non-volatile-restore corner case),
	 * so the delayed save is safe.
	 */
	(void)TL_ZB_TIMER_SCHEDULE(nwk_ed_minimal_deferred_zb_info_save_timer,
				   NULL, 15000U);
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
	g_nwkEdCtx.fixedJoinValid = FALSE;
	g_nwkEdCtx.fixedJoinChannel = 0xFFU;
	g_nwkEdCtx.fixedJoinPanId = MAC_INVALID_PANID;
	g_nwkEdCtx.fixedJoinShortAddr = MAC_SHORT_ADDR_NONE;
	memset(g_nwkEdCtx.fixedJoinExtPanId, 0, sizeof(g_nwkEdCtx.fixedJoinExtPanId));
	memset(g_nwkEdCtx.fixedJoinNwkKey, 0, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
	g_nwkEdCtx.fixedJoinUsesPreconfiguredNwkKey = FALSE;
	ZB_IEEE_ADDR_ZERO(g_nwkEdCtx.fixedJoinTcAddr);
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
	g_nwkEdCtx.interviewKickScheduled = FALSE;
	g_nwkEdCtx.interviewPollCount = 0U;
	g_nwkEdCtx.interviewPollIntervalMs = NWK_ED_MINIMAL_INTERVIEW_POLL_MS;
	g_nwkEdCtx.endDevTimeoutRspSeen = FALSE;
	g_nwkEdCtx.endDevTimeoutReqScheduled = FALSE;
	g_nwkEdCtx.rxEvtDropCount = 0U;
	g_nwkEdCtx.rxEvtOverflowCount = 0U;
	g_nwkEdCtx.lastRxEvtDropType = NWK_ED_MINIMAL_RX_EVT_NONE;
	g_nwkEdCtx.beaconEbusyRetry = 0U;
	g_nwkEdCtx.assocEbusyRetry = 0U;
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
		if (g_nwkEdCtx.parentCandidateValid) {
			g_nwkEdCtx.activeChannel = g_nwkEdCtx.activeScanChannel;
			g_nwkEdCtx.activePanId = g_nwkEdCtx.candidatePanId;
			g_nwkEdCtx.activeParentShortAddr = g_nwkEdCtx.parentCandidateShortAddr;
			ZB_EXTPANID_COPY(g_nwkEdCtx.activeExtPanId, g_nwkEdCtx.candidateExtPanId);
		} else {
			g_nwkEdCtx.activeChannel = g_nwkEdCtx.fixedJoinChannel;
			g_nwkEdCtx.activePanId = g_nwkEdCtx.fixedJoinPanId;
			g_nwkEdCtx.activeParentShortAddr = g_nwkEdCtx.fixedJoinShortAddr;
			ZB_EXTPANID_COPY(g_nwkEdCtx.activeExtPanId, g_nwkEdCtx.fixedJoinExtPanId);
			g_nwkEdCtx.activeShortAddr = g_zbMacPib.shortAddress;
		}
		if (!nwk_ed_minimal_send_rejoin_request()) {
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
	zb_nwk_ed_interview_trace[1] = 0xb7e20000U | g_nwkEdCtx.state;
	nwk_ed_minimal_timer_cancel();
	nwk_ed_minimal_timeout_req_cancel();
	ev_unon_timer(&g_nwkEdCtx.opTimer);
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
	g_nwkEdCtx.discoveryForRejoin = FALSE;
	g_nwkEdCtx.interviewKickScheduled = FALSE;
	g_nwkEdCtx.endDevTimeoutReqScheduled = FALSE;
}

void tl_zbNwkEdMinimalOperationComplete(u8 status)
{
	zb_nwk_ed_interview_trace[2] = 0xb7e30000U | ((u32)status << 8) |
				       g_nwkEdCtx.state;
	g_nwkEdCtx.lastJoinStatus = status;
	nwk_ed_minimal_timeout_req_cancel();
	ev_unon_timer(&g_nwkEdCtx.opTimer);
	g_nwkEdCtx.state = NWK_ED_MINIMAL_STATE_IDLE;
	g_nwkEdCtx.rejoinWithBackoff = FALSE;
	g_nwkEdCtx.discoveryForRejoin = FALSE;
	g_nwkEdCtx.interviewKickScheduled = FALSE;
	g_nwkEdCtx.endDevTimeoutReqScheduled = FALSE;
}

bool tl_zbNwkEdMinimalManagerIdle(void)
{
	return g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_IDLE;
}

bool tl_zbNwkEdMinimalCanProcessDataFrames(void)
{
	return g_zbNwkCtx.joined ||
	       (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW) ||
	       (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_JOINING) ||
	       (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN);
}

void tl_zbNwkEdMinimalTransportKeyDone(void)
{
	zb_nwk_ed_interview_trace[3] = 0xb7e40000U | g_nwkEdCtx.state;
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_INTERVIEW) {
		return;
	}

	if (TL_SCHEDULE_TASK(nwk_ed_minimal_transport_key_done_task, NULL) != RET_OK) {
		LOG_ERR("join complete: failed to schedule transport-key handoff");
	}
}

void tl_zbNwkEdMinimalRejoinResponseReceived(u8 status, u16 nwkAddr, u16 parentShortAddr)
{
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_REJOIN ||
	    parentShortAddr != g_nwkEdCtx.activeParentShortAddr) {
		return;
	}

	if (status != MAC_SUCCESS || nwkAddr >= ZB_MAC_SHORT_ADDR_NOT_ALLOCATED) {
		LOG_WRN("rejoin response rejected: status 0x%02x nwk 0x%04x parent 0x%04x",
			status, nwkAddr, parentShortAddr);
		nwk_ed_minimal_finish_join(ZDO_NETWORK_LOST, TRUE);
		return;
	}

	nwk_ed_minimal_apply_tc_context();
	g_zbMacPib.panId = g_nwkEdCtx.activePanId;
	g_zbMacPib.shortAddress = nwkAddr;
	g_zbMacPib.coordShortAddress = parentShortAddr;
	g_zbMacPib.associatedPanCoord = TRUE;
	g_zbMacPib.phyChannelCur = g_nwkEdCtx.activeChannel;
	g_nwkEdCtx.activeShortAddr = nwkAddr;
	zb_radio_port_update_filters(g_nwkEdCtx.activePanId, nwkAddr, g_zbMacPib.extAddress);

	g_zbNIB.panId = g_nwkEdCtx.activePanId;
	g_zbNIB.nwkAddr = nwkAddr;
	g_zbNIB.depth = 1U;
	ZB_EXTPANID_COPY(g_zbNIB.extPANId, g_nwkEdCtx.activeExtPanId);
	nwk_ed_minimal_install_fixed_join_key_if_needed();
	nwk_ed_minimal_sync_zb_info_from_runtime();

	if (!aps_ib.aps_authenticated) {
		LOG_INF("rejoin response accepted: short 0x%04x pan 0x%04x parent 0x%04x, waiting transport key",
			nwkAddr, g_nwkEdCtx.activePanId, parentShortAddr);
		tl_zdoEdMinimalAssocDone(ZDO_SUCCESS, TRUE);
		nwk_ed_minimal_enter_interview(TRUE);
		return;
	}

	nwk_ed_minimal_complete_join(TRUE);
}

static void nwk_ed_minimal_post_join_announce_task(void *arg)
{
	ARG_UNUSED(arg);

	if (!g_zbNwkCtx.joined) {
		return;
	}

	/* Arm the interview poll loop before sending the announce so the
	 * coordinator's ZDO queries are received even if the announce is lost. */
	tl_zbNwkEdMinimalInterviewPollStart(0U, 0U);
	if (zb_zdoSendDevAnnance() == ZDO_SUCCESS) {
		nwk_ed_minimal_timeout_req_schedule(NWK_ED_MINIMAL_TIMEOUT_REQ_DELAY_MS);
	}
}

static void nwk_ed_minimal_transport_key_done_task(void *arg)
{
	ARG_UNUSED(arg);

	zb_post_tk_trace[13] = 0xd0000000U | (zb_post_tk_trace[13] + 1U) |
				((u32)g_nwkEdCtx.state << 16);

	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_INTERVIEW) {
		return;
	}

	nwk_ed_minimal_complete_join(g_nwkEdCtx.interviewRejoinMode);
}

static void nwk_ed_minimal_post_join_poll_task(void *arg)
{
	ARG_UNUSED(arg);

	zb_post_tk_trace[12] = 0xc0000000U | (zb_post_tk_trace[12] + 1U);
	tl_zbNwkEdMinimalPollEnsure();
	bdb_ed_runtime_join_complete();
	zb_post_tk_trace[12] |= 0x00008000U;
	nwk_ed_minimal_joined_idle_poll_schedule(nwk_ed_minimal_effective_poll_rate());
}

static int nwk_ed_minimal_post_join_poll_timer(void *arg)
{
	nwk_ed_minimal_post_join_poll_task(arg);
	return -1;
}

static void nwk_ed_minimal_interview_kick_task(void *arg)
{
	ARG_UNUSED(arg);

	nwk_ed_minimal_timer_trace_put((0x04U << 24) |
				       ((u32)g_nwkEdCtx.state << 16) |
				       g_nwkEdCtx.assocPollCount);
	g_nwkEdCtx.interviewKickScheduled = FALSE;
	if (g_nwkEdCtx.state != NWK_ED_MINIMAL_STATE_INTERVIEW) {
		return;
	}

	if (nwk_ed_minimal_send_data_request()) {
		g_nwkEdCtx.assocPollCount++;
	}

	nwk_ed_minimal_timer_start(NWK_ED_MINIMAL_INTERVIEW_POLL_MS);
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
	if (g_nwkEdCtx.activePanId == MAC_INVALID_PANID ||
	    g_nwkEdCtx.activeParentShortAddr == MAC_SHORT_ADDR_NONE) {
		return;
	}

	if (count == 0U) {
		count = NWK_ED_MINIMAL_INTERVIEW_POLL_MAX;
	}

	g_nwkEdCtx.interviewPollCount = count;
	g_nwkEdCtx.interviewPollIntervalMs =
		(intervalMs != 0U) ? intervalMs : NWK_ED_MINIMAL_INTERVIEW_POLL_MS;
	nwk_ed_minimal_timer_trace_put((0x06U << 24) |
				       ((u32)g_nwkEdCtx.state << 16) |
				       ((u32)count << 8) |
				       (u8)(g_zbNwkCtx.joined ? 1U : 0U));

	if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW ||
	    g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_JOINING ||
	    g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN) {
		nwk_ed_minimal_timer_start(1U);
		return;
	}

	if (!g_zbNwkCtx.joined) {
		return;
	}

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
		g_nwkEdCtx.fixedJoinUsesPreconfiguredNwkKey =
			nwk_ed_minimal_key_is_set(g_nwkEdCtx.fixedJoinNwkKey);
	} else {
		memset(g_nwkEdCtx.fixedJoinNwkKey, 0, sizeof(g_nwkEdCtx.fixedJoinNwkKey));
		g_nwkEdCtx.fixedJoinUsesPreconfiguredNwkKey = FALSE;
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
		if (!nwk_ed_minimal_fixed_join_target_locked()) {
			tl_zbNwkEdMinimalSetFixedJoinTarget(
				evt->beacon.channel, evt->beacon.panId,
				evt->beacon.parentShortAddr, evt->beacon.extPanId,
				have_profile && profile.network_key_valid ? profile.network_key : NULL,
				have_profile && profile.tc_addr_valid ? profile.tc_addr : NULL);
		}
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

	/*
	 * Fresh joins may need to attach through a router parent when beacon
	 * responses are missing or stale. Treat observed directed/broadcast MAC
	 * traffic as a parent hint for both discovery modes and prefer coordinator
	 * hints when they are visible.
	 */
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
		if (!nwk_ed_minimal_fixed_join_target_locked()) {
			tl_zbNwkEdMinimalSetFixedJoinTarget(evt->traffic.channel, evt->traffic.panId,
							    evt->traffic.parentShortAddr, NULL,
							    NULL, NULL);
		}
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
			tl_zdoEdMinimalAssocDone(ZDO_SUCCESS, rejoinMode);
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
		bool associationPermit;

		zb_nwk_beacon_frame_count++;
		zb_nwk_beacon_last_len = payloadLen;
		zb_nwk_beacon_last_fcf = hdr.fcf;
		if (!discoveryActive) {
			zb_nwk_beacon_discovery_inactive_count++;
			return;
		}

		memset(&evt, 0, sizeof(evt));
		if (!nwk_ed_minimal_parse_beacon_candidate(macPld, payloadLen, &hdr, &panId,
							   &parentShortAddr, extPanId,
							   &associationPermit)) {
			zb_nwk_beacon_parse_fail_count++;
			return;
		}
		/*
		 * The beacon permit bit is a useful hint but not a reliable hard
		 * filter in mixed coordinator/router networks: permit-join state may
		 * lag across devices while association admission remains authoritative.
		 */
		ARG_UNUSED(associationPermit);

		zb_nwk_beacon_success_count++;
		zb_nwk_ed_trace[8]++;
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
		/*
		 * Some coordinators send the first interview payload immediately
		 * after the Association Response, before the deferred join-event
		 * task runs. Pre-arm the short-address context and radio filters
		 * here so that an immediate Transport Key frame to the assigned
		 * short address is accepted instead of being dropped against the
		 * old broadcast filter window.
		 */
		if (assocStatus == MAC_SUCCESS &&
		    evt.assocRsp.assignedShortAddr < ZB_MAC_SHORT_ADDR_NOT_ALLOCATED &&
		    hdr.srcShortValid &&
		    hdr.srcShortAddr == g_nwkEdCtx.activeParentShortAddr &&
		    (!hdr.dstExtValid ||
		     memcmp(hdr.dstExtAddr, g_zbMacPib.extAddress, sizeof(addrExt_t)) == 0) &&
		    (!hdr.dstShortValid ||
		     hdr.dstShortAddr == MAC_SHORT_ADDR_BROADCAST ||
		     hdr.dstShortAddr == g_zbMacPib.shortAddress)) {
			g_nwkEdCtx.activeShortAddr = evt.assocRsp.assignedShortAddr;
			g_zbMacPib.panId = g_nwkEdCtx.activePanId;
			g_zbMacPib.shortAddress = evt.assocRsp.assignedShortAddr;
			g_zbMacPib.coordShortAddress = g_nwkEdCtx.activeParentShortAddr;
			g_zbMacPib.associatedPanCoord = TRUE;
			g_zbMacPib.phyChannelCur = g_nwkEdCtx.activeChannel;
			if (hdr.srcExtValid) {
				ZB_IEEE_ADDR_COPY(g_zbMacPib.coordExtAddress, hdr.srcExtAddr);
			}
			g_zbNIB.panId = g_nwkEdCtx.activePanId;
			g_zbNIB.nwkAddr = evt.assocRsp.assignedShortAddr;
			ZB_EXTPANID_COPY(g_zbNIB.extPANId, g_nwkEdCtx.activeExtPanId);
			zb_radio_port_update_filters(g_nwkEdCtx.activePanId,
					     evt.assocRsp.assignedShortAddr,
					     g_zbMacPib.extAddress);
		}
		if (nwk_ed_minimal_rx_evt_push(&evt)) {
			(void)TL_SCHEDULE_TASK(nwk_ed_minimal_rx_event_task, NULL);
		}
	}
}
