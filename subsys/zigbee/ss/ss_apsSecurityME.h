/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_APS_SECURITY_ME_H
#define _SS_APS_SECURITY_ME_H

#include "zb_common.h"
#include "ss_security_flags.h"

extern ss_info_base_t ss_ib;
#if defined(ZB_COORDINATOR_ROLE)
extern ss_tc_keyPair_t g_ssTcKeyPair[];
#else
extern ss_dev_keyPair_t g_ssDevKeyPair;
#endif

u8 ss_devKeyPairFind(addrExt_t extAddr, ss_dev_pair_set_t *keyPair);
bool ss_securityModeIsDistributed(void);

#if defined(ZB_COORDINATOR_ROLE)
void ss_tcKeyPairClear(ss_tc_keyPair_t *keyPair);
void ss_tcKeyPairPeriodic(void);
#endif
void ss_apsmeRequestKeyReq(void *arg);
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
void ss_apsmeUpdateDevReq(void *arg);
void ss_apsmeRemoveDeviceReq(void *arg);
void ss_apsRemoveDeviceCmdHandle(void *arg);
void ss_apsTunnelCmdHandle(void *arg);
#endif
void ss_apsmeTransportKeyReq(void *arg);
void ss_apsmeSwitchKeyReq(void *arg);
#if defined(ZB_COORDINATOR_ROLE)
void ss_apsUpdateDeviceCmdHandle(void *arg);
u8 ss_apsVerifyKeyCmdValid(void *keyPair, u8 keyType);
void ss_apsVerifyKeyCmdHandle(void *arg);
void ss_apsRequestKeyCmdHandle(void *arg);
#endif
#if !defined(ZB_COORDINATOR_ROLE)
void ss_apsmeVerifyKeyReq(void *arg);
#endif
#if !defined(ZB_COORDINATOR_ROLE)
void ss_apsTransportKeyCmdHandle(void *arg);
void ss_apsConfirmKeyCmdHandle(void *arg);
void ss_apsSwitchKeyCmdHandle(void *arg);
#endif

#endif
