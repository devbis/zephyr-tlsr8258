/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_ENDDEV_TIMEOUT_H
#define _NWK_ENDDEV_TIMEOUT_H

#include "zb_common.h"

typedef struct {
	u32 timeout;
	addrExt_t extAddr;
} nwk_endDevTimeout_nv_t;

void nwkEndDevTimeoutReqCnfHandler(void *arg);
void nwkEndDevTimeoutReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkEndDevTimeoutRspCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd);
void nwkEndDevTimeoutRspCnfHandler(void *arg);
void nwkEndDevTimeoutRejoin(void);

#endif
