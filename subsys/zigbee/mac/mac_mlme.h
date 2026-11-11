/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_MLME_H
#define _MAC_MLME_H

#include "zb_common.h"

void zb_PhyMlmeIndicate(void *arg, uint8_t *raw, uint8_t len);
void zb_MacResetRequestHandler(void *arg);
void zb_MacPollRequestHandler(void *arg);
void zb_MacStartRequestHandler(void *arg);
void zb_MacStartReqConfirm(void *arg, uint8_t status);
void zb_macTimerEventProc(void *arg);
void zb_MacCommStatusSend(void *arg, uint8_t status);
#if defined(ZB_COORDINATOR_ROLE)
void zb_MacCmdPanIdConflictNotifySendCheck(void *arg, uint8_t status);
#endif

#endif
