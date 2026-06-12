/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-compatible replacement for zigbee/common/includes/zb_common.h.
 * Pulls in all headers that SDK source files expect to find via zb_common.h.
 */
#pragma once

#include "tl_platform.h"
#include <string.h>
#include <zephyr/random/random.h>

#ifndef ZB_BUF_SIZE
#define ZB_BUF_SIZE 164
#endif

/* zb_buf_t / zb_buf_hdr_t mirror the SDK layout (tl_zigbee_sdk
 * zigbee/common/includes/zb_buffer.h). The struct stays binary
 * compatible with libzigbee's zb_buffer.c so router NWK sources can
 * reach into buf->hdr.handle / buf->hdr.used the way the vendor code
 * expects.
 */
typedef struct {
	u8 id;              /* primitive id */
	u8 handle;
	s8 rssi;
	u8 used:1;
	u8 macTxFifo:1;
	u8 leaveRejoin:1;
	u8 active:1;        /* only for mac command buffer */
	u8 pending:1;       /* only for endDev: parent has pending data */
	u8 rejoinStartAgain:1;
	u8 resvHdr:2;
} zb_buf_hdr_t;

typedef struct zb_buf_s {
	u8  buf[ZB_BUF_SIZE];
	zb_buf_hdr_t hdr;
	struct zb_buf_s *next;
	u32 allocCnt;
	u32 freeCnt;
} zb_buf_t;

#ifndef ZB_BUF_POOL_NUM
#define ZB_BUF_POOL_NUM 18
#endif
#ifndef SEC_KEY_LEN
#define SEC_KEY_LEN 16
#endif

/* Task queue API matching SDK's tl_zb_callback_t / tl_zbTaskPost signature */
typedef void (*tl_zb_callback_t)(void *arg);
u8 tl_zbTaskPost(tl_zb_callback_t fn, void *arg);
#define TL_SCHEDULE_TASK tl_zbTaskPost

/* Layer-queue identifiers used by the SDK macros wrapping
 * tl_zbPrimitivePost(layerQ, primitive, arg). Matches
 * tl_zigbee_sdk/zigbee/common/includes/zb_task_queue.h.
 */
enum {
	TL_Q_EV_TASK = 0,
	TL_Q_MAC2NWK,
	TL_Q_NWK2MAC,
	TL_Q_HIGH2NWK,
	TL_Q_NWK2HIGH,
	TL_Q_TYPE_MAX,
};

u8 tl_zbPrimitivePost(u8 layerQ, u8 primitive, void *arg);

/* NWK protocol version (mirrors tl_zigbee_sdk
 * zigbee/common/includes/zb_config.h).
 */
#ifndef ZB_PROTOCOL_VERSION
#define ZB_PROTOCOL_VERSION    2
#endif

extern const u8 g_zero_addr[8];
extern u32 g_secondCnt;

/* Generic return codes used broadly across SDK sources. */
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

/* System diagnostics counters (mirrors zb_common.h sys_diagnostics_t) */
typedef struct {
	u16 numberOfResets;
	u16 persistentMemoryWrites;
	u32 macRxBcast;
	u32 macTxBcast;
	u32 macRxUcast;
	u32 macTxUcast;
	u16 macTxUcastRetry;
	u16 macTxUcastFail;
	u16 nwkTxCnt;
	u16 nwkTxEnDecryptFail;
	u16 apsRxBcast;
	u16 apsTxBcast;
	u16 apsRxUcast;
	u16 apsTxUcastSuccess;
	u16 apsTxUcastRetry;
	u16 apsTxUcastFail;
	u16 routeDiscInitiated;
	u16 neighborAdded;
	u16 neighborRemoved;
	u16 neighborStale;
	u16 joinIndication;
	u16 childMoved;
	u32 panIdConflictCheck;
	u16 nwkFCFailure;
	u16 apsFCFailure;
	u16 apsUnauthorizedKey;
	u16 nwkDecryptFailures;
	u16 apsDecryptFailures;
	u16 packetBufferAllocateFailures;
	u16 relayedUcast;
	u16 phytoMACqueuelimitreached;
	u16 packetValidateDropCount;
	u8  lastMessageLQI;
	s8  lastMessageRSSI;
	u8  macTxIrqTimeoutCnt;
	u8  macTxIrqCnt;
	u8  macRxIrqCnt;
	u32 macRxCrcFail;
	u8  phyLengthError;
	u8  panIdConflict;
	u8  panIdModified;
	u8  nwkAddrConflict;
} sys_diagnostics_t;
extern sys_diagnostics_t g_sysDiags;

/* ─── Common address/utility macros (mirrors zb_common.h) ─────────────── */
#include "common/utility.h"

#define EXT_ADDR_LEN                    8
#define TL_SETSTRUCTCONTENT(s, v)       (memset((u8 *)&(s), (v), sizeof(s)))
#define COPY_U16TOBUFFER(buf, data)     do { \
	(((u8 *)(buf))[0]) = (u8)(data); \
	(((u8 *)(buf))[1]) = (u8)((data) >> 8); \
} while (0)
#define COPY_BUFFERTOU16(data, buf)     do { \
	(data) = ((buf)[0]) + (((buf)[1]) << 8); \
} while (0)
#define COPY_U32TOBUFFER(buf, data)     do { \
	(((u8 *)(buf))[0]) = (u8)(data); \
	(((u8 *)(buf))[1]) = (u8)((data) >> 8); \
	(((u8 *)(buf))[2]) = (u8)((data) >> 16); \
	(((u8 *)(buf))[3]) = (u8)((data) >> 24); \
} while (0)
#define ZB_IS_64BIT_ADDR_ZERO(addr)     (!memcmp((addr), g_zero_addr, EXT_ADDR_LEN))
#define ZB_IS_64BIT_ADDR_INVALID(addr)  (!memcmp((addr), g_invalid_addr, EXT_ADDR_LEN))
#define ZB_64BIT_ADDR_ZERO(addr)        (memset((addr), 0, EXT_ADDR_LEN))
#define ZB_64BIT_ADDR_COPY(dst, src)    (memcpy((dst), (src), EXT_ADDR_LEN))
#define ZB_64BIT_ADDR_CMP(a, b)         ((bool)!memcmp((a), (b), EXT_ADDR_LEN))

/* Channel mask covering 802.15.4 channels 11..26 (bits 11..26 set).
 * Mirrors tl_zigbee_sdk zigbee/common/includes/zb_common.h.
 */
#ifndef ZB_TRANSCEIVER_ALL_CHANNELS_MASK
#define ZB_TRANSCEIVER_ALL_CHANNELS_MASK   0x07FFF800UL
#endif
#define ZB_EXTPANID_IS_ZERO             ZB_IS_64BIT_ADDR_ZERO
#define ZB_EXTPANID_COPY                ZB_64BIT_ADDR_COPY
#define ZB_EXTPANID_CMP                 ZB_64BIT_ADDR_CMP
#define ZB_IEEE_ADDR_INVALID(addr)      ZB_64BIT_ADDR_COPY((addr), g_invalid_addr)
#define ZB_IEEE_ADDR_COPY               ZB_64BIT_ADDR_COPY
#define ZB_IEEE_ADDR_ZERO               ZB_64BIT_ADDR_ZERO
#define ZB_IEEE_ADDR_IS_INVALID         ZB_IS_64BIT_ADDR_INVALID
#define ZB_IEEE_ADDR_IS_ZERO            ZB_IS_64BIT_ADDR_ZERO
extern const addrExt_t g_invalid_addr;
extern const addrExt_t g_zero_addr;

/* Frame header sizes (mirrors zb_common.h) */
#ifndef ZB_MAC_FRAME_HEADER
#define ZB_MAC_FRAME_HEADER             (9 + 2)
#endif
#ifndef ZB_NWK_FRAME_HEADER
#define ZB_NWK_FRAME_HEADER             (8 + NWK_MAX_SOURCE_ROUTE * 2 + 14 + 4)
#endif
#ifndef ZB_APS_FRAME_HEADER
#define ZB_APS_FRAME_HEADER             10
#endif

/* Telink manufacturer code */
#ifndef MANUFACTURER_CODE_TELINK
#define MANUFACTURER_CODE_TELINK        0x1141
#endif

#ifndef DEBUG
#define DEBUG(...)                      do { } while (0)
#endif

#include <zephyr/zigbee/zb_config.h>
#include "os/ev_buffer.h"
#include "os/ev_queue.h"
#include "os/ev_timer.h"
#include "os/ev_poll.h"
#include "os/ev.h"
#include "drv_hw.h"
#include "drv_security.h"
#include "drv_radio.h"
#include "drv_nv.h"
#include "mac/includes/mac_phy.h"
#include "mac/includes/mac_trx_api.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_neighbor.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_nib.h"
#include "nwk/includes/nwk_ctx.h"
#include "aps/aps_api.h"

/* Define apsdeDataInd_t here so af/zb_af.h can use OFFSETOF(apsdeDataInd_t, asdu).
 * zbapi/zb_api.h will skip its own copy when it sees the guard. */
#ifndef ZB_APSDE_DATA_IND_DEFINED
#define ZB_APSDE_DATA_IND_DEFINED
typedef struct apsdeDataInd_s {
	aps_data_ind_t indInfo;
	u16 asduLen;
	u8  asdu[];
} apsdeDataInd_t;
#endif

/* Include order matters: af → ss → zdo/zdp → zdo/zdo_api → zbapi/zb_api */
#include "af/zb_af.h"
#include "ss/security_service.h"
#include "zdo/zdp.h"
#include "zdo/zdo_api.h"
#include "zbapi/zb_api.h"
#include "bdb/includes/bdb.h"
#include "zcl/zll_commissioning/zcl_touchlink_attr.h"

enum {
	REJOIN_INSECURITY,
	REJOIN_SECURITY,
};

static inline u16 zb_random(void)
{
	return (u16)sys_rand32_get();
}

#define ZB_RANDOM() zb_random()

/* Global stack state shape expected by open Zigbee sources. */
typedef struct {
	tl_zb_mac_pib_t macPib;
	nwk_nib_t nwkNib;
	touchlink_attr_t touchlinkAttr;
	bdb_attr_t bdbAttr;
} zb_info_t;
extern zb_info_t g_zbInfo;

#define g_zbMacPib      g_zbInfo.macPib
#define g_zbNIB         g_zbInfo.nwkNib
#define g_touchlinkAttr g_zbInfo.touchlinkAttr
#define g_bdbAttrs      g_zbInfo.bdbAttr

extern const u8 tcLinkKeyCentralDefault[];
extern const u8 linkKeyDistributedCertification[];
extern const u8 linkKeyDistributedMaster[];
void zb_info_save(void *arg);
void zb_reset(void);
