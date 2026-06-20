/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Router-side definition of zb_nwk_ed_trace. The ED build pulls it
 * from nwk_ed_minimal.c, but that TU isn't linked for the router
 * build. Bump to 24 slots so the libzigbee chain probes (slots 0..10)
 * coexist with zb_main.c bootstrap markers (11..15) and leave headroom
 * for further checkpoints.
 */
#include "zb_common_stub.h"

volatile u32 zb_nwk_ed_trace[45] = {
	[0] = 0x4e574b45U,
	/* slots beyond the 32-word boundary need explicit zero initializers
	 * to ride along in the .data LMA copy that runs at boot — without
	 * them they keep stale SRAM contents from prior firmware images. */
	[33] = 0U,
	[34] = 0U,
	[35] = 0U,
	[36] = 0U,
	[37] = 0U,
	[38] = 0U,
	[39] = 0U,
	[40] = 0U,
	[41] = 0U,
	/* slot[42] = mac_rxDataParse call count (Layer 3 diag)
	 * slot[43] = zdo_nlme_join_confirm state/status (ZDO diag) */
	[42] = 0U,
	[43] = 0U,
	/* slot[44] = aps_command_handle call count (APS layer diag) */
	[44] = 0U,
};
