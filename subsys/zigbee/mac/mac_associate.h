/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_ASSOCIATE_H
#define _MAC_ASSOCIATE_H

#include "zb_common.h"

extern void *associationReqOrigBuffer;

void mac_pendingWaitTimerCancel(void);
void zb_MacAssociateRequestStatusCheck(void *arg, uint8_t status);
void zb_MacAssociateRequestHandler(void *arg);
#if defined(ZB_ROUTER_ROLE)
void zb_MacAssociateResponseHandler(void *arg);
#endif
void zb_MacDisassociateNotifyCmdConfirm(void *arg, uint8_t status);
void zb_MacDisassociateRequestHandler(void *arg);
void zb_MlmeCmdDisassociateNotifyRecvd(void *arg, void *raw);
void zb_MacAssocPollConfirm(uint8_t status);
void zb_MacAssociateRespReceived(void);

#endif
