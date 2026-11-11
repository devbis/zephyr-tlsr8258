/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_nwk_addr_map.h"
#include "zb_nwk_address_assign.h"

uint16_t zb_NwkStochasticAddrCal(void)
{
	while (1) {
		uint16_t candidate = (uint16_t)drv_u32Rand();

		if (candidate == 0U || candidate >= 0xFFF7U) {
			continue;
		}

		if (candidate == g_zbMacPib.shortAddress) {
			continue;
		}

		addrExt_t extAddr;
#if defined(ZB_ROUTER_ROLE) || defined(ZB_COORDINATOR_ROLE)
		uint16_t addrMapIdx;
		uint16_t *addrMapIdxPtr = &addrMapIdx;
#else
		uint16_t *addrMapIdxPtr = NULL;
#endif
		if (zb_ExtAddrByShortAddr(candidate, extAddr, addrMapIdxPtr) == RET_OK) {
			continue;
		}

		return candidate;
	}
}
