/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_SCAN_H
#define _MAC_SCAN_H

#include "zb_common.h"

void tl_zbMacScanRequestHandler(zb_mac_mlme_scan_req_t *req);
u8 tl_zbMacMlmeBeaconRequestCmdSend(void);
void tl_zbMacActiveScanListAdd(void);
void tl_zbMacOrphanScanStatusUpdate(void);

#endif
