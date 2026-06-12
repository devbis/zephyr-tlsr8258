/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Stochastic short-address allocator used by the router formation /
 * association response path.
 *
 * Adapted from libzigbee/src/nwk_address_assign.c (~25 LOC). One-line
 * change vs. the vendor file: vendor "zb_local.h" → zb_common_stub.h
 * + nwk/includes. Otherwise verbatim.
 */

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_addr_map.h"

#if defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE

u16 tl_zbNwkStochasticAddrCal(void)
{
	while (1) {
		u16 candidate = (u16)drv_u32Rand();

		if (candidate == 0U || candidate >= 0xFFF7U) {
			continue;
		}

		if (candidate == g_zbMacPib.shortAddress) {
			continue;
		}

		addrExt_t extAddr;
		if (tl_zbExtAddrByShortAddr(candidate, extAddr, NULL) == RET_OK) {
			continue;
		}

		return candidate;
	}
}

#endif /* ZB_ROUTER_ROLE */
