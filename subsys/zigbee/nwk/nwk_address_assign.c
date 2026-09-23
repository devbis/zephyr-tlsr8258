/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_nwk_addr_map.h"
#include "zb_nwk_address_assign.h"

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
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
		u16 addrMapIdx;
		u16 *addrMapIdxPtr = &addrMapIdx;
#else
		u16 *addrMapIdxPtr = NULL;
#endif
		if (tl_zbExtAddrByShortAddr(candidate, extAddr, addrMapIdxPtr) == RET_OK) {
			continue;
		}

		return candidate;
	}
}
