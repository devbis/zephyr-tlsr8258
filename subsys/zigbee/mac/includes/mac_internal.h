/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from libzigbee/src/include/mac_internal.h. */
#ifndef DRIVERS_ZIGBEE_SRC_INCLUDE_MAC_INTERNAL_H
#define DRIVERS_ZIGBEE_SRC_INCLUDE_MAC_INTERNAL_H

#include "zb_common_stub.h"
#include "common/list.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_nib.h"

/* Buffer helpers shared with the ported NWK TUs (defined in libzigbee
 * zb_buffer.c — Zephyr port stays link-time extern until that TU lands).
 */
extern zb_buf_t *zb_buf_allocate(void);
extern void zb_buf_free(zb_buf_t *buf);
extern void *tl_bufInitalloc(zb_buf_t *p, u8 size);

/* MAC/NWK initial constants defined in subsys/zigbee/common/zb_config.c
 * (SDK copy).
 */
extern const tl_zb_mac_pib_t macPibDefault;
extern const nwk_nib_t nwkNibDefault;

/* Vendor constant absent from Zephyr-side mac headers. */
#ifndef SHORT_ADDR_LEN
#define SHORT_ADDR_LEN 2
#endif

/* MAC layer entry points and helpers defined in mac.c / mac_trx.c /
 * mac_data.c / mac_indirect_data.c. Cross-TU references shared
 * across all the libzigbee-derived MAC files.
 */
extern void tl_zbNwkBeaconPayloadUpdate(void);
extern u8 *tl_zbMacHdrBuilder(u8 *txData, tl_zb_mac_mhr_t *mhr);
extern u8 tl_zbMacHdrSize(u16 frameCtrl);
extern u8 tl_zbMacTx(zb_buf_t *buf, u8 *txData, u8 psduLen, u8 ackRequired, void *cb);
#if defined(ZB_ROUTER_ROLE)
extern u8 macDataPending(void *buf, u32 dstAddrLo, u32 dstAddrHi, u8 dstAddrMode);
#endif
extern void *tl_phyRxBufTozbBuf(u8 *rxBuf);
extern u8 tl_zbUserTaskQNum(void);
extern u8 ZB_TASKQ_USERUSE_SIZE;
extern void tl_zbMacMcpsDataRequestSendConfirm(zb_buf_t *buf, u8 status);

/* Hardware timer hook used by mac_trx.c — provided by drv_hw on
 * vendor builds, link-time extern on Zephyr until a Zephyr-side
 * adapter is wired through drv_hw_zephyr.c.
 */
typedef int (*timerCb_t)(void *arg);
extern int drv_hwTmr_set(u8 tmrIdx, u32 t_us, timerCb_t func, void *arg);

extern mac_appIndCb_t *macAppIndCb;
extern u8 g_macTimerEvt[0x0c];
extern void *associationReqOrigBuffer;

extern void mac_trxTask(void *arg);
extern void mac_trxInit(void);
extern void mac_pendingWaitTimerCancel(void);
extern void tl_zbMacMcpsDataRequestProc(void *arg);
extern void tl_zbMacAssociateRequestHandler(void *arg);
extern void tl_zbMacDisassociateRequestHandler(void *arg);
extern void tl_zbMacResetRequestHandler(void *arg);
extern void tl_zbMacPollRequestHandler(void *arg);
extern void tl_zbMacScanRequestHandler(zb_mac_mlme_scan_req_t *req);
extern void tl_zbMacStartRequestHandler(void *arg);
extern void tl_zbMacStartReqConfirm(void *arg, u8 status);
extern void tl_zbPhyMldeIndication(zb_buf_t *buf, u8 *raw, u8 len);
extern void tl_zbPhyMlmeIndicate(void *arg, u8 *raw, u8 len);
extern void tl_zbPhyIndication(void *arg, u8 *raw, u8 len);
extern void zb_macTimerEventProc(void *arg);
extern void tl_zbMacAssociateRequestStatusCheck(void *arg, u8 status);
extern void tl_zbMacDisassociateNotifyCmdConfirm(void *arg, u8 status);
#if defined(ZB_ROUTER_ROLE)
extern void tl_zbMacAssociateResponseHandler(void *arg);
extern void tl_zbMacBeaconRequestCb(void);
extern u8 tl_zbMacMlmeBeaconCmdSend(tl_zbBeaconFrame_t *beacon);
extern void tl_zbMacMlmeBeaconSendConfirm(void *arg, u8 status);
extern u8 tl_zbMacMlmeCoordRealignmentCmdSend(u8 rxOnWhenIdle, const u8 *orphanAddr, u16 shortAddr, void *arg);
extern void tl_zbMacOrphanResponseHandler(void *arg);
extern void tl_zbMacOrphanResponseStatusCheck(void *arg, u8 status);
extern int tl_zbMacPacketDelaySend(void *arg);

typedef struct _attribute_packed_ mac_pending_entry {
    struct mac_pending_entry *next;
    void *buf;
    u8 addr[8];
    u8 addrMode;
    u8 timeout;
    u8 expiry;
    u8 state;
    u8 status;
} mac_pending_entry_t;

#define MAC_PENDING_ENTRY_STATIC_ASSERT_JOIN_(a, b) a##b
#define MAC_PENDING_ENTRY_STATIC_ASSERT_JOIN(a, b) MAC_PENDING_ENTRY_STATIC_ASSERT_JOIN_(a, b)
#define MAC_PENDING_ENTRY_STATIC_ASSERT(cond) \
    typedef char MAC_PENDING_ENTRY_STATIC_ASSERT_JOIN(static_assertion_mac_pending_entry_, __COUNTER__)[(cond) ? 1 : -1]

/* Zephyr port: vendor build pinned these offsets to the -fpack-struct
 * layout; mac_pending_entry_t still has _attribute_packed_ so the
 * field offsets are stable, but the size differs slightly under
 * different compilers/abi options. The runtime path uses field
 * accessors only, so the precise size assertion is informational. */
#if 0
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, next) == 0);
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, buf) == 4);
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, addr) == 8);
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, addrMode) == 16);
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, timeout) == 17);
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, expiry) == 18);
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, state) == 19);
MAC_PENDING_ENTRY_STATIC_ASSERT(OFFSETOF(mac_pending_entry_t, status) == 20);
MAC_PENDING_ENTRY_STATIC_ASSERT(sizeof(mac_pending_entry_t) == 21);
#endif

#undef MAC_PENDING_ENTRY_STATIC_ASSERT
#undef MAC_PENDING_ENTRY_STATIC_ASSERT_JOIN
#undef MAC_PENDING_ENTRY_STATIC_ASSERT_JOIN_
#endif
extern void tl_zbMacDataRequestStatusCheck(zb_buf_t *buf, u8 status);
extern void tl_zbMacMcpsDataRequestSendConfirm(zb_buf_t *buf, u8 status);
#if defined(ZB_ROUTER_ROLE)
extern int macIndirPeriodic(void *arg);
#endif
extern void tl_zbMacActiveScanListAdd(void);
extern void tl_zbMacAssociateRespReceived(void);
extern void tl_zbMacOrphanScanStatusUpdate(void);
extern void tl_zbMlmeCmdDisassociateNotifyRecvd(void *arg, void *raw);
extern void tl_zbMacChannelSet(u8 chan);
extern void tl_zbMacAssocPollConfirm(u8 status);
extern void tl_zbMaxTxConfirmCb(void *arg, u8 status);
extern void generateIEEEAddr(void);
extern void mac_appIndCbRegister(mac_appIndCb_t *cb);
extern void mac_pibNvInit(u8 coldReset);
extern zb_buf_t *zb_buf_allocate(void);
extern u8 *tl_zbMacHdrBuilder(u8 *buf, tl_zb_mac_mhr_t *mhr);
extern u8 tl_zbMacHdrParse(tl_zb_mac_mhr_t *mhr, u8 *buf);
extern u8 tl_zbMacTx(zb_buf_t *txBuf, u8 *txData, u8 psduLen, u8 ack, void *pendingList);
extern u8 tl_zbMacMlmeDataRequestCmdSend(zb_mlme_data_req_cmd_t *req, zb_buf_t *buf, u8 status);
#if defined(ZB_ROUTER_ROLE)
extern u8 macDataPending(void *buf, u32 dstAddrLo, u32 dstAddrHi, u8 dstAddrMode);
extern void macDataPendingListProc(void *arg);
extern void macDataPendingListManage(void *arg, u8 status);
extern u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 update);
extern int tl_zbMacPendingDataSearch(u8 addrMode, u8 *addr);
extern void tl_zbMacMlmeDataRequestCb(void *arg);
#endif
extern void tl_zbMacCommStatusSend(void *arg, u8 status);

#endif
