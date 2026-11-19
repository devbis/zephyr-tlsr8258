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
void ss_zdoNwkKeySwitch(uint8_t keySeqNum);
void ss_zdoNwkKeyConfigure(uint8_t *key, uint8_t keySeqNum, bool active);
void ss_zdoLinkKeyConfigure(addrExt_t extAddr, uint8_t *key, uint8_t keyAttr, uint8_t apsLinkKeyType);
void ss_zdoUseKey(uint8_t keySeqNum);
uint8_t ss_keyIsEmpty(const uint8_t *key);
void *ss_zdoGetNwkKeyBySeqNum(uint8_t seqNum);
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void ss_zdoUpdateDeviceIndHandle(void *arg);
#endif
#if defined(ZB_COORDINATOR_ROLE)
uint8_t ss_zdoAcceptNewDevAllow(void);
void ss_zdoTcInit(void);
void ss_tcSwitchKeyTimerStart(void);
void ss_tcTransportKeyTimerStart(void *arg);
void ss_tcSwitchKey(uint8_t keySeqNum);
#endif

#endif
