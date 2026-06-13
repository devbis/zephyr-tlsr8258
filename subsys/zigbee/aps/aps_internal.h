/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from libzigbee/src/include/aps_internal.h. */
#ifndef DRIVERS_ZIGBEE_SRC_INCLUDE_APS_INTERNAL_H
#define DRIVERS_ZIGBEE_SRC_INCLUDE_APS_INTERNAL_H

#include "zb_common_stub.h"
#include "aps/aps_api.h"

typedef struct _attribute_packed_ {
	zb_buf_t *txBuf;
	u8 *adu;
	tl_zb_addr_t dstAddr;
	u8 addrMode;
	u8 aduLen;
	u8 secure;
	u8 secureNwkLayer;
	u8 reserved;
} aps_cmd_send_req_t;

typedef struct {
	zb_buf_t *active_buf;
	u32 active_ep_count;
	u8 active_eps[APS_EP_NUM_IN_GROUP_TBL];
	u8 pending_refs[4];
	u8 pending_rptr;
	u8 pending_wptr;
} aps_group_q_t;

extern u16 dstPanID;
extern u8 g_apsTxCacheNum;
extern u8 deviceInfoRsp;
extern aps_group_q_t aps_group_q;

extern void af_dataCnfHandler(void *arg);
extern void af_aps_data_entry(void *arg);
extern void af_aps_data_fragment_entry(void *arg);
extern void aps_nwk_addr_req_cb(void *arg);
extern void aps_nwk_data_confirm_cb(void *arg);
extern void aps_nwk_data_indication_cb(void *arg);
extern void aps_interPanDataIndCb(void *arg);
extern int aps_data_fragment_delay(void *arg);
extern int apsDuplicatePeriodic(void *arg);
extern int apsAckPeriodic(void *arg);
extern void aps_data_fragment(void *arg);
extern void aps_data_request(void *arg);
extern u8 aps_get_handle(void);
extern void aps_command_handle(void *arg);
extern void aps_process_group_addressed_packet(zb_buf_t *buf);
extern u8 *aps_group_ep_info_get(u16 group_addr, u8 *epNum);
extern void aps_me_init(void);
extern void aps_deliver_group_msg(void *arg);
extern void aps_groupTblReset(void);
extern void aps_init_group_num_set(void);
extern void aps_cmd_send(void *arg, u8 handle);
extern aps_tx_cache_list_t *apsTxDataPost(u8 ackNeed, u8 addrReqNeed, u8 interPan,
					   void *payload, u8 *cnf);
extern void apsTxEventPost(aps_tx_cache_list_t *cache, u8 event, u8 status);
extern u8 aps_bindingTblMatched(u16 clusterId, u8 srcEp);
extern u8 aps_bindingTblExist(addrExt_t extAddr);

/* SS forward decls — defined in libzigbee ss_*.c, link-time externs
 * until those TUs are ported. Signatures mirror the vendor call sites
 * verbatim, accepting opaque void* args where the vendor passes
 * implementation-specific pointers.
 */
extern u8 ss_apsEnAuxHdrFill(u8 *auxHdr, void *keyInfo, u8 extNonceOpt);
extern u8 ss_apsSecureFrame(void *p, u8 apsHdrAuxLen, u8 apsHdrLen, addrExt_t extAddr);
extern u8 ss_apsDecryptFrame(void *arg);
extern void ss_apsTransportKeyCmdHandle(void *arg);
extern void ss_apsRemoveDeviceCmdHandle(void *arg);
extern void ss_apsSwitchKeyCmdHandle(void *arg);
extern void ss_apsRequestKeyCmdHandle(void *arg);
extern void ss_apsUpdateDeviceCmdHandle(void *arg);
extern void ss_apsVerifyKeyCmdHandle(void *arg);
extern void ss_apsConfirmKeyCmdHandle(void *arg);
extern void ss_apsTunnelCmdHandle(void *arg);

/* NWK forward decls used by aps_data.c inter-PAN path. */
extern void tl_zbNwkInterPanDataReq(void *arg);

/* Misc helpers consumed by aps_*.c. */
extern void secondClockStop(void);

/* Buffer-ref macros mirror tl_zigbee_sdk zb_buffer.h. The vendor uses
 * a packed buffer pool (g_mPool) so the ref is the buffer index; we
 * have no such pool yet, so the macros stand in as link-time names
 * (the aps group queue that consumes them stays dead-stripped on the
 * Zephyr runtime path).
 */
extern zb_buf_t g_zb_buf_ref_dummy;
#define ZB_BUF_FROM_REF(ref) (&g_zb_buf_ref_dummy + (ref))
#define ZB_REF_FROM_BUF(p)   ((u8)((p) - &g_zb_buf_ref_dummy))

#endif
