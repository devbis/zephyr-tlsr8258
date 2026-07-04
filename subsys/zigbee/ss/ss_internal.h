/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from libzigbee/src/include/ss_internal.h. */
#ifndef DRIVERS_ZIGBEE_SRC_INCLUDE_SS_INTERNAL_H
#define DRIVERS_ZIGBEE_SRC_INCLUDE_SS_INTERNAL_H

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "ss/security_service.h"

extern ss_info_base_t ss_ib;
extern ss_dev_keyPair_t g_ssDevKeyPair;

extern u8 ss_apsSecureFrame(void *p, u8 apsHdrAuxLen, u8 apsHdrLen, addrExt_t extAddr);
extern u8 ss_apsEnAuxHdrFill(u8 *auxHdr, void *keyInfo, u8 extNonceOpt);
extern u8 ss_apsDecryptFrame(void *arg);
extern u8 ss_nwkDecryptFrame(void *p, u8 nwkHdrSize, u8 payloadSize, u8 *payloadAddr, nwk_hdr_t *nwkHdr, u8 lqi);
extern u8 ss_nwkSecureFrame(zb_buf_t *src, u8 nwkHdrAuxLen);
extern u8 ss_ccmEncryption(u8 *key, u8 *nonce, u8 nwkHdrLen, u8 *nwkHdr, u8 srcMsgLen, u8 *srcMsg);
extern u8 ss_ccmDecryption(u8 *key, u8 *nonce, u8 nwkHdrLen, u8 *nwkHdr, u8 srcMsgLen, u8 *srcMsg);
extern u8 ss_keyHash(u8 *padV, u8 *key, u8 *hashOut);
extern u8 ss_devKeyPairFind(addrExt_t extAddr, ss_dev_pair_set_t *keyPair);
extern bool ss_securityModeIsDistributed(void);
extern void ss_zdoTransportKeyIndHandle(void *arg);
extern void ss_zdoRemoveDeviceIndHandle(void *arg);
extern void *ss_zdoGetNwkKeyBySeqNum(u8 seqNum);
extern void ss_zdoInsecureRejoin(void *arg);
extern void ss_zdoUseKey(u8 keySeqNum);
extern void ss_zdoNwkKeySwitch(u8 keySeqNum);
extern void ss_zdoNwkKeyConfigure(u8 *key, u8 keySeqNum, bool active);
extern void ss_apsmeVerifyKeyReq(void *arg);
extern void ss_apsmeRequestKeyReq(void *arg);
extern void ss_apsmeTransportKeyReq(void *arg);
extern void ss_apsmeSwitchKeyReq(void *arg);
#if defined(ZB_ROUTER_ROLE) || defined(ZB_ED_ROLE_LIBZIGBEE)
extern void ss_apsmeUpdateDevReq(void *arg);
extern void ss_apsmeRemoveDeviceReq(void *arg);
extern void ss_apsRemoveDeviceCmdHandle(void *arg);
extern void ss_apsTunnelCmdHandle(void *arg);
extern void ss_zdoChildTableStore(void *arg);
#endif
extern void ss_apsConfirmKeyCmdHandle(void *arg);
extern void ss_apsSwitchKeyCmdHandle(void *arg);
extern void ss_apsTransportKeyCmdHandle(void *arg);

#endif
