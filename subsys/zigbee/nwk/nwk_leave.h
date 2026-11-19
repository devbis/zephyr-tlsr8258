/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_LEAVE_H
#define _NWK_LEAVE_H

#include "zb_common.h"

void nwk_leaveCmdSendCnf(void *arg, uint16_t dstAddr);
int nwkLeaveReqSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, uint8_t handle);

#endif
