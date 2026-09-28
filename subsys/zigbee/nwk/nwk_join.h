/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_JOIN_H
#define _NWK_JOIN_H

#include "zb_common.h"

void nwk_nlmeJoinCnf(void *arg, u8 status);
void nwk_rejoinScanCnfHandler(void *arg);
void nwk_directJoinScanCnfHandler(void *arg);
void nwk_rejoinCmdSendCnf(void *arg);
void tl_zbNwkSendRejoinRespCmd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 ack, u8 handle);

#endif
