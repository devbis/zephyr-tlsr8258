/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_CR_COORDINATOR_H
#define _MAC_CR_COORDINATOR_H

#include "zb_common.h"

#if defined(ZB_ROUTER_ROLE)
uint8_t zb_MacMlmeBeaconCmdSend(zb_BeaconFrame_t *beacon);
uint8_t zb_MacMlmeCoordRealignmentCmdSend(uint8_t rxOnWhenIdle, const uint8_t *orphanAddr, uint16_t shortAddr,
				       void *arg);
void zb_MacBeaconRequestCb(void);
void zb_MacMlmeBeaconSendConfirm(void *arg, uint8_t status);
void zb_MacOrphanResponseHandler(void *arg);
void zb_MacOrphanResponseStatusCheck(void *arg, uint8_t status);
int zb_MacPacketDelaySend(void *arg);
#endif

#endif
