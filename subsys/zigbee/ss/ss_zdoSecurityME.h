/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_ZDO_SECURITY_ME_H
#define _SS_ZDO_SECURITY_ME_H

#include "zb_common.h"
#include "ss_security_flags.h"

#if !defined(ZB_COORDINATOR_ROLE)
int ss_devKeyPairTimeoutCb(void *arg);
#endif
void ss_zdoInsecureRejoin(void *arg);
#if !defined(ZB_COORDINATOR_ROLE)
void ss_zdoTransportKeyIndHandle(void *arg);
#endif
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void ss_zdoChildTableStore(void *arg);
void ss_zdoChildAuthStart(void *arg);
void ss_zdoRemoveDeviceIndHandle(void *arg);
#endif
void ss_zdoNwkKeySwitch(u8 keySeqNum);
void ss_zdoNwkKeyConfigure(u8 *key, u8 keySeqNum, bool active);
void ss_zdoLinkKeyConfigure(addrExt_t extAddr, u8 *key, u8 keyAttr, u8 apsLinkKeyType);
void ss_zdoUseKey(u8 keySeqNum);
u8 ss_keyIsEmpty(const u8 *key);
void *ss_zdoGetNwkKeyBySeqNum(u8 seqNum);
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void ss_zdoUpdateDeviceIndHandle(void *arg);
#endif
#if defined(ZB_COORDINATOR_ROLE)
u8 ss_zdoAcceptNewDevAllow(void);
void ss_zdoTcInit(void);
void ss_tcSwitchKeyTimerStart(void);
void ss_tcTransportKeyTimerStart(void *arg);
void ss_tcSwitchKey(u8 keySeqNum);
#endif

#endif
