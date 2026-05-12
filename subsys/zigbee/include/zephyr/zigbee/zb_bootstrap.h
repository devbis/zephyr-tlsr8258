/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

/*
 * Optional application hooks invoked by the Zigbee platform bootstrap thread.
 * Applications may override any of these functions.
 */
void zb_platform_app_bootstrap_ready(void);
bool zb_platform_app_enable_radio_smoke_probe(void);
bool zb_platform_app_should_start_commissioning(void);
void zb_platform_app_start_commissioning(void);
