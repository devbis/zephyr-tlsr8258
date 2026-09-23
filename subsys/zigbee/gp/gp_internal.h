/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _GP_INTERNAL_H
#define _GP_INTERNAL_H

#include "zb_common.h"

extern gp_data_req_pending_t gpTxQueue;
extern gpDeviceAnnounceCheckCb_t g_gpDeviceAnnounceCheckCb;
extern u8 zclGpAttr_gpSharedSecKeyType;
extern u8 zclGpAttr_gpSharedSecKey[];

void cGpDataCnfHandler(void *arg);
void dGp_dataInd(void *arg);
int gpDataIndDuplicatePeriodic(void *arg);

#endif
