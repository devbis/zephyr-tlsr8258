/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Port of the vendor libzigbee umbrella header (src/include/zb_local.h),
 * adapted to the Zephyr port's header names/layout. The vendor .c files
 * include "zb_local.h" at file scope; in this port most were rewritten to
 * include the specific headers directly, but the ED-role (#else) branches of
 * several vendor sources still `#include "zb_local.h"`. Provide the umbrella so
 * the experimental CONFIG_ZIGBEE_ED_LIBZIGBEE build resolves them.
 */
#ifndef ZB_LOCAL_H
#define ZB_LOCAL_H

#include "zb_common_stub.h"          /* vendor zb_common.h equivalent */
#include "aps/aps_internal.h"
#include "mac/includes/mac_internal.h"
#include "nwk/includes/nwk_internal.h"
#include "ss/ss_internal.h"
#include "zdo/zdo_internal.h"

/* vendor zb_internal.h */
extern void tl_zbMacMcpsDataConfirmHandler(void *arg);
extern void tl_zbMacMcpsDataIndicationHandler(void *arg);

#endif /* ZB_LOCAL_H */
