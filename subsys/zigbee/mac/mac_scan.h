/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_SCAN_H
#define _MAC_SCAN_H

#include "zb_common.h"

void zb_MacScanRequestHandler(zb_mac_mlme_scan_req_t *req);
uint8_t zb_MacMlmeBeaconRequestCmdSend(void);
void zb_MacActiveScanListAdd(void);
void zb_MacOrphanScanStatusUpdate(void);

#endif
