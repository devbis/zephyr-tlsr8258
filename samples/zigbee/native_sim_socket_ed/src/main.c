/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "app_bdb.h"

LOG_MODULE_REGISTER(main);

volatile uint32_t zb_restore_diag_trace[16] = { 0xa5d10000U };

bool zb_platform_app_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target)
{
	return app_bdb_get_fixed_join_target(target);
}

bool zb_platform_app_get_join_profile(struct zb_platform_bdb_join_profile *profile)
{
	return app_bdb_get_join_profile(profile);
}

void zb_platform_app_bootstrap_ready(void)
{
	app_bdb_bootstrap_ready();
}

bool zb_platform_app_enable_radio_smoke_probe(void)
{
	return false;
}

bool zb_platform_app_should_start_commissioning(void)
{
	return app_bdb_should_start_commissioning();
}

void zb_platform_app_start_commissioning(void)
{
	app_bdb_start_commissioning();
}

void zb_platform_app_network_left(void)
{
	app_bdb_network_left();
}

void zb_platform_app_bdb_commissioning_status(uint8_t status, bool joined_network)
{
	app_bdb_commissioning_status(status, joined_network);
}

int main(void)
{
	LOG_INF("native_sim socket Zigbee ED ready");
	return 0;
}
