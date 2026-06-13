/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_test.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / mac_trx_api.h / ev_timer.h / mac_phy.h
 * are replaced by the Zephyr include set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "mac/includes/mac_phy.h"
#include "mac/includes/mac_trx_api.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"
#include "mac/includes/mac_internal.h"
