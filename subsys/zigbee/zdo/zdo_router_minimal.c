/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Router-mode ZDO helpers. Symmetric counterpart to zdo_ed_minimal.c.
 *
 * zdo_cfg_attributes itself is declared in subsys/zigbee/zdo/zdo.c (the
 * libzigbee port). zdo.c only declares it — it leaves the struct
 * zero-initialized, which makes zdo_nwkDiscoveryStart() return
 * ZDO_NOT_PERMITTED because config_nwk_scan_attempts == 0.
 *
 * Populate the attributes here via a SYS_INIT() so the router build
 * gets non-zero defaults without conflicting with zdo.c's tentative
 * definition. ED build keeps its own static initializer in
 * zdo_ed_minimal.c (not linked for router).
 */

#include <zephyr/init.h>

#include "zb_common_stub.h"
#include "zdo_api.h"

static int zdo_router_minimal_attr_init(void)
{
	zdo_cfg_attributes.config_nwk_indirectPollRate = 1000U;
	zdo_cfg_attributes.config_nwk_time_btwn_scans = 100U;
	/* Match libzigbee BDB: only one attempt is needed because the
	 * BDB layer drives the retry loop itself. Higher values cause
	 * zdo_nlme_network_discovery_confirm_cb to queue more rounds
	 * via zdo_nwkDiscReqTimerCb instead of returning to BDB, which
	 * then never reaches bdb_nwkDiscCnfCb -> zb_assocJoinReq.
	 */
	zdo_cfg_attributes.config_nwk_scan_attempts = 1U;
	zdo_cfg_attributes.config_permit_join_duration = 0U;
	zdo_cfg_attributes.config_parent_link_retry_threshold = 3U;
	zdo_cfg_attributes.config_accept_nwk_update_pan_id = 0xFFFFU;
	zdo_cfg_attributes.config_accept_nwk_update_channel = 0xFFU;
	zdo_cfg_attributes.config_nwk_scan_duration = 5U;
	return 0;
}

SYS_INIT(zdo_router_minimal_attr_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
