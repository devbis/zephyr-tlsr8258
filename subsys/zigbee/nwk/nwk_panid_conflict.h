/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_PANID_CONFLICT_H
#define _NWK_PANID_CONFLICT_H

#include "zb_common.h"

void nwk_panIdConflictCnfHandler(void *arg);
bool tl_zbNwkPanidConflictDetect(u16 panId, extPANId_t epid);
void tl_zbNwkPanidConflictProcess(void *arg);
void tl_zbNwkPanidConflictSetPanidStart(void);
void nwkReportCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle);
void nwkReportCmdHandler(void *arg, nwkCmd_t *cmd);

#endif
