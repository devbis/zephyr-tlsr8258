/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Optional application hooks invoked by the Zigbee platform bootstrap thread.
 * Applications may override any of these functions.
 */
void zb_platform_app_bootstrap_ready(void);
bool zb_platform_app_enable_radio_smoke_probe(void);
bool zb_platform_app_should_start_commissioning(void);
void zb_platform_app_start_commissioning(void);

/*
 * Platform-managed BDB helpers used by lightweight samples.
 * These are no-ops when BDB is not enabled.
 */
int zb_platform_bdb_init_default(void);
uint8_t zb_platform_bdb_network_steer_start(void);
int zb_platform_restore_persistent_state(void);
