/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_JOIN_H
#define _NWK_JOIN_H

#include "zb_common.h"

void nwk_nlmeJoinCnf(void *arg, uint8_t status);
void nwk_rejoinScanCnfHandler(void *arg);
void nwk_directJoinScanCnfHandler(void *arg);
void nwk_rejoinCmdSendCnf(void *arg);
void zb_NwkSendRejoinRespCmd(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, uint8_t ack, uint8_t handle);

#endif
