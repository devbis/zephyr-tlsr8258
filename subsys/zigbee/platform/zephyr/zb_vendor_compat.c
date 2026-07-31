/* SPDX-License-Identifier: Apache-2.0 */

#include "drv_hw.h"
#include "drv_security.h"
#include "zb_common_stub.h"

/*
 * Vendor-compatibility globals for the unified libzigbee stack (both roles).
 * The MAC TX queue lives here; the former minimal-ED lightweight MAC/APS/SS
 * helpers were removed when the ED was unified onto the libzigbee core, which
 * now provides tl_zbMacInit / aps_init / the SS hash helpers for both roles.
 */

/* APS / NWK / MAC table sizes and arrays now live in
 * subsys/zigbee/common/zb_config.c (copied from
 * tl_zigbee_sdk/zigbee/common/zb_config.c). MAC_TX_QUEUE_SIZE /
 * g_txQueue stay here because zb_config.c gates them behind
 * ZB_ZEPHYR_TX_QUEUE_IN_VENDOR_COMPAT.
 */
u8 MAC_TX_QUEUE_SIZE = TX_QUEUE_BN;
tx_data_queue g_txQueue[TX_QUEUE_BN];

/* g_zbMacCtx is defined by the libzigbee-derived mac.c port when
 * present, else here as a weak fallback for the ED build.
 */
tl_zb_mac_ctx_t g_zbMacCtx __attribute__((weak));


