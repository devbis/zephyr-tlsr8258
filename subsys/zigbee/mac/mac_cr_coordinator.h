/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_CR_COORDINATOR_H
#define _MAC_CR_COORDINATOR_H

#include "zb_common.h"

#if defined(ZB_ROUTER_ROLE)
u8 tl_zbMacMlmeBeaconCmdSend(tl_zbBeaconFrame_t *beacon);
u8 tl_zbMacMlmeCoordRealignmentCmdSend(u8 rxOnWhenIdle, const u8 *orphanAddr, u16 shortAddr,
				       void *arg);
void tl_zbMacBeaconRequestCb(void);
void tl_zbMacMlmeBeaconSendConfirm(void *arg, u8 status);
void tl_zbMacOrphanResponseHandler(void *arg);
void tl_zbMacOrphanResponseStatusCheck(void *arg, u8 status);
int tl_zbMacPacketDelaySend(void *arg);
#endif

#endif
