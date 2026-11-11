/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_PANID_CONFLICT_H
#define _NWK_PANID_CONFLICT_H

#include "zb_common.h"

void nwk_panIdConflictCnfHandler(void *arg);
bool zb_NwkPanidConflictDetect(uint16_t panId, extPANId_t epid);
void zb_NwkPanidConflictProcess(void *arg);
void zb_NwkPanidConflictSetPanidStart(void);
void nwkReportCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, uint8_t handle);
void nwkReportCmdHandler(void *arg, nwkCmd_t *cmd);

#endif
