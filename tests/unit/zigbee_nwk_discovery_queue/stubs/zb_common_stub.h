/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

typedef u8 addrExt_t[8];
typedef u8 extPANId_t[8];

typedef void (*tl_zb_callback_t)(void *arg);
u8 tl_zbTaskPost(tl_zb_callback_t fn, void *arg);
#define TL_SCHEDULE_TASK tl_zbTaskPost

enum {
	RET_OK = 0,
	RET_ERROR,
	RET_BLOCKED,
	RET_EXIT,
	RET_BUSY,
	RET_EOF,
	RET_OUT_OF_RANGE,
	RET_EMPTY,
	RET_CANCELLED,
	RET_PENDING,
	RET_NO_MEMORY,
	RET_INVALID_PARAMETER,
	RET_OPERATION_FAILED,
	RET_BUFFER_TOO_SMALL,
	RET_END_OF_LIST,
	RET_ALREADY_EXISTS,
	RET_NOT_FOUND,
};

#define TRUE 1
#define FALSE 0
#define SUCCESS 0
#define FAILURE 1
#define BIT(n) (1U << (n))
#define ARG_UNUSED(x) ((void)(x))

#define EXT_ADDR_LEN 8
#define SEC_KEY_LEN 16
#define SECUR_N_SECUR_MATERIAL 2
#define SS_PRECONFIGURED_NWKKEY BIT(0)
#define SS_SEMODE_CENTRALIZED 1
#define SS_SEMODE_DISTRIBUTED 0

#define MAC_FCF_FIELD_LEN 2U
#define MAC_SEQ_NUM_FIELD_LEN 1U
#define MAC_PAN_ID_FIELD_LEN 2U
#define MAC_SHORT_ADDR_FIELD_LEN 2U
#define MAC_EXT_ADDR_FIELD_LEN 8U
#define MAC_MIN_HDR_LEN 3U

#define MAC_FRAME_BEACON 0U
#define MAC_FRAME_DATA 1U
#define MAC_FRAME_COMMAND 3U
#define FRAME_TYPE_COMMAND 1U

#define MAC_FCF_FRAME_TYPE_POS 0U
#define MAC_FCF_FRAME_TYPE_MASK 0x0007U
#define MAC_FCF_INTRA_PAN_MASK BIT(6)
#define MAC_FCF_ACK_REQ_BIT BIT(5)
#define MAC_FCF_DST_ADDR_MODE_POS 10U
#define MAC_FCF_DST_ADDR_MODE_MASK (0x3U << MAC_FCF_DST_ADDR_MODE_POS)
#define MAC_FCF_SRC_ADDR_MODE_POS 14U
#define MAC_FCF_SRC_ADDR_MODE_MASK (0x3U << MAC_FCF_SRC_ADDR_MODE_POS)

#define ZB_ADDR_NO_ADDR 0U
#define ZB_ADDR_16BIT_DEV_OR_BROADCAST 2U
#define ZB_ADDR_64BIT_DEV 3U

#define MAC_CMD_ASSOCIATION_REQUEST 0x01U
#define MAC_CMD_ASSOCIATION_RESPONSE 0x02U
#define MAC_CMD_DATA_REQUEST 0x04U

#define MAC_INVALID_PANID 0xffffU
#define MAC_SHORT_ADDR_NONE 0xffffU
#define MAC_SHORT_ADDR_BROADCAST 0xffffU
#define ZB_MAC_SHORT_ADDR_NOT_ALLOCATED 0xfffeU

#define TL_ZB_MAC_CHANNEL_STOP 26U
#define POLL_RATE 1000U
#define REQTIMEOUTENUM_INVALID 0xffU
#define NWK_ENDDEV_TIMEOUT_DEFAULT 8U
#define TIMEOUT_RSP_STATUS_SUCCESS 0U
#define MAC_SUCCESS 0U

#define NLME_STATE_IDLE 0U
#define NLME_IDLE 0U

#define ZDO_SUCCESS 0x00U
#define ZDO_TIMEOUT 0x85U
#define ZDO_NO_MATCH 0x86U
#define ZDO_NOT_SUPPORTED 0x84U
#define ZDO_NOT_PERMITTED 0x88U
#define ZDO_TABLE_FULL 0x8cU
#define ZDO_NETWORK_LOST 0xcdU
#define NWK_STATUS_SUCCESS 0x00U
#define MAC_STA_PAN_AT_CAPACITY 0x01U
#define MAC_STA_PAN_ACCESS_DENIED 0x02U

#define MAC_PHY_ATTR_CURRENT_CHANNEL 0U

#define TL_SETSTRUCTCONTENT(s, v) memset(&(s), (v), sizeof(s))
#define COPY_U16TOBUFFER(buf, data) do { \
	((u8 *)(buf))[0] = (u8)(data); \
	((u8 *)(buf))[1] = (u8)((data) >> 8); \
} while (0)
#define COPY_U32TOBUFFER(buf, data) do { \
	((u8 *)(buf))[0] = (u8)(data); \
	((u8 *)(buf))[1] = (u8)((data) >> 8); \
	((u8 *)(buf))[2] = (u8)((data) >> 16); \
	((u8 *)(buf))[3] = (u8)((data) >> 24); \
} while (0)

#define ZB_64BIT_ADDR_COPY(dst, src) memcpy((dst), (src), EXT_ADDR_LEN)
#define ZB_64BIT_ADDR_ZERO(dst) memset((dst), 0, EXT_ADDR_LEN)
#define ZB_IS_64BIT_ADDR_ZERO(addr) (!memcmp((addr), g_zero_addr, EXT_ADDR_LEN))
#define ZB_IS_64BIT_ADDR_INVALID(addr) (!memcmp((addr), g_invalid_addr, EXT_ADDR_LEN))
#define ZB_IEEE_ADDR_COPY ZB_64BIT_ADDR_COPY
#define ZB_IEEE_ADDR_ZERO ZB_64BIT_ADDR_ZERO
#define ZB_IEEE_ADDR_INVALID(addr) ZB_64BIT_ADDR_COPY((addr), g_invalid_addr)
#define ZB_IEEE_ADDR_IS_ZERO ZB_IS_64BIT_ADDR_ZERO
#define ZB_IEEE_ADDR_IS_INVALID ZB_IS_64BIT_ADDR_INVALID
#define ZB_EXTPANID_COPY ZB_64BIT_ADDR_COPY

typedef struct {
	u8 altPanCoord;
	u8 devType;
	u8 powerSrc;
	u8 secuCapability;
} nwk_capability_info_t;

typedef struct {
	addrExt_t extAddress;
	addrExt_t coordExtAddress;
	u16 panId;
	u16 shortAddress;
	u16 coordShortAddress;
	u8 associatedPanCoord;
	u8 phyChannelCur;
	u8 rxOnWhenIdle;
} tl_zb_mac_pib_t;

typedef struct {
	u8 endDevTimeoutDefault;
	u16 nwkAddr;
	u8 seqNum;
	nwk_capability_info_t capabilityInfo;
	u16 panId;
	u8 depth;
	extPANId_t extPANId;
	u8 parentInfo;
} nwk_nib_t;

typedef struct {
	u8 joined;
	u8 is_factory_new;
	u8 parentIsChanged;
	u8 state;
	u8 user_state;
} nwk_ctx_t;

typedef struct {
	u8 key[SEC_KEY_LEN];
	u8 keySeqNum;
	u8 keyType;
} ss_material_set_t;

typedef struct {
	addrExt_t trust_center_address;
	u8 activeSecureMaterialIndex;
	u8 activeKeySeqNum;
	u8 preConfiguredKeyType;
	u32 outgoingFrameCounter;
	ss_material_set_t nwkSecurMaterialSet[SECUR_N_SECUR_MATERIAL];
} ss_ib_t;

typedef struct {
	u8 aps_authenticated;
} aps_ib_t;

typedef struct {
	u8 nodeIsOnANetwork;
} bdb_attr_t;

typedef struct {
	tl_zb_mac_pib_t macPib;
	nwk_nib_t nwkNib;
	bdb_attr_t bdbAttr;
} zb_info_t;

typedef struct {
	u16 phytoMACqueuelimitreached;
} sys_diagnostics_t;

typedef struct zb_platform_bdb_join_profile {
	bool pan_id_valid;
	u16 pan_id;
	bool ext_pan_id_valid;
	extPANId_t ext_pan_id;
	u32 channel_mask;
	bool network_key_valid;
	u8 network_key[SEC_KEY_LEN];
	bool tc_addr_valid;
	addrExt_t tc_addr;
} zb_platform_bdb_join_profile;

typedef struct {
	bool warmStart;
} nlme_reset_req_t;

typedef struct {
	u8 status;
} nlme_reset_cnf_t;

typedef struct {
	void (*zdpResetCnfCb)(nlme_reset_cnf_t *cnf);
} zdo_appIndCb_t;

extern zb_info_t g_zbInfo;
extern nwk_ctx_t g_zbNwkCtx;
extern ss_ib_t ss_ib;
extern aps_ib_t aps_ib;
extern sys_diagnostics_t g_sysDiags;
extern zdo_appIndCb_t *zdoAppIndCbLst;
extern const addrExt_t g_invalid_addr;
extern const addrExt_t g_zero_addr;

#define g_zbMacPib g_zbInfo.macPib
#define g_zbNIB g_zbInfo.nwkNib
#define g_bdbAttrs g_zbInfo.bdbAttr

void zb_info_save(void *arg);
u8 tl_zbMacAttrSet(u8 attr, const void *value, u8 len);
void ss_securityModeSet(u8 mode);
u8 zb_platform_radio_send_raw_psdu(const u8 *frame, u8 len);
int zb_platform_radio_send_beacon_request(void);
bool zb_platform_app_get_join_profile(struct zb_platform_bdb_join_profile *profile);
u8 rf_getChannel(void);
u8 zb_zdoSendDevAnnance(void);
u32 zdo_af_get_syn_rate(void);
void bdb_ed_runtime_join_complete(void);
void tl_zdoEdMinimalDiscoveryDone(u8 status);
void tl_zdoEdMinimalJoinDone(u8 status, bool rejoinMode);
u8 zb_minimal_ccm_encrypt_auth(const u8 *key, const u8 *nonce, u8 mic_len,
				 const u8 *aad, u8 aad_len, const u8 *payload, u8 payload_len,
				 u8 *out);
u8 nv_nwkFrameCountSaveToFlash(u32 frame_counter);
u8 ZB_MAC_DSN(void);
void ZB_INC_MAC_DSN(void);
