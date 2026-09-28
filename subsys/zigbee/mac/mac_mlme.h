/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_MLME_H
#define _MAC_MLME_H

#include "zb_common.h"

void tl_zbPhyMlmeIndicate(void *arg, u8 *raw, u8 len);
void tl_zbMacResetRequestHandler(void *arg);
void tl_zbMacPollRequestHandler(void *arg);
void tl_zbMacStartRequestHandler(void *arg);
void tl_zbMacStartReqConfirm(void *arg, u8 status);
void zb_macTimerEventProc(void *arg);
void tl_zbMacCommStatusSend(void *arg, u8 status);
#if defined(ZB_COORDINATOR_ROLE)
void tl_zbMacCmdPanIdConflictNotifySendCheck(void *arg, u8 status);
#endif

#endif
