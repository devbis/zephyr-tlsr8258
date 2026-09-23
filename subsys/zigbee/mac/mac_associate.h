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
void tl_zbMacAssociateRequestStatusCheck(void *arg, u8 status);
void tl_zbMacAssociateRequestHandler(void *arg);
#if defined(ZB_ROUTER_ROLE)
void tl_zbMacAssociateResponseHandler(void *arg);
#endif
void tl_zbMacDisassociateNotifyCmdConfirm(void *arg, u8 status);
void tl_zbMacDisassociateRequestHandler(void *arg);
void tl_zbMlmeCmdDisassociateNotifyRecvd(void *arg, void *raw);
void tl_zbMacAssocPollConfirm(u8 status);
void tl_zbMacAssociateRespReceived(void);

#endif
