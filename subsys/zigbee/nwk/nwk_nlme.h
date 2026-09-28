/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_NLME_H
#define _NWK_NLME_H

#include "zb_common.h"

void nwk_nlmeStartRouterCnf(void *arg, u8 status);
void nwk_startRouterCnfHandler(void *arg);
void nwk_edScanCnfHandler(void *arg);

#endif
