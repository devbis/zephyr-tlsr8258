/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-native router-mode NWK runtime stub.
 *
 * Companion to nwk_ed_minimal.c. Provides the symbols invoked by the
 * Zephyr BDB layer when CONFIG_ZIGBEE_ROUTER=y. The actual router-side
 * commissioning state machine (NLME-NETWORK-FORMATION, permit-join,
 * link-status broadcasts, child management) is being ported in stages.
 *
 * Reference (do not copy verbatim): vendor-derived libzigbee
 * src/nwk_formation.c, src/nwk_permit_joining.c, src/nwk_brc.c,
 * src/nwk_neighbor.c. Logic is adapted to drive Zephyr primitives:
 * ev_timer scheduling, zb_radio_port_* helpers, and the existing
 * ed-minimal MAC/IO scaffolding shared between roles.
 */

#include "zb_common_stub.h"
#include "mac/includes/mac_phy.h"
#include "os/ev_timer.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_config.h>

LOG_MODULE_REGISTER(zigbee_nwk_router_minimal, CONFIG_ZIGBEE_LOG_LEVEL);

/*
 * Router commissioning entrypoint invoked by app_bdb when
 * CONFIG_ZIGBEE_ROUTER=y. Returns 0 to indicate the request was
 * accepted; non-zero defers retry via app_bdb's retry timer.
 *
 * TODO: implement NLME-NETWORK-FORMATION request, channel scan,
 * PAN-ID selection, NWK key generation, and post-formation
 * link-status broadcast scheduling.
 */
uint8_t zb_routerStart(void)
{
	LOG_WRN("zb_routerStart: router runtime not implemented yet");
	return 1U; /* signal "busy/retry" so app_bdb keeps polling */
}
