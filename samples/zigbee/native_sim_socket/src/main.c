/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_zbhci.h>

#include "app_bdb.h"

LOG_MODULE_REGISTER(main);

bool zb_platform_app_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target)
{
	/*
	 * A joining node learns the PAN and its parent from the active-scan
	 * beacons, and the coordinator forms its own network, so there is no
	 * fixed join target.
	 */
	ARG_UNUSED(target);
	return false;
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
#if defined(CONFIG_ZIGBEE_ZBHCI_UART)
	/* uart1 exists only in the coordinator devicetree overlay. */
	int rc = zb_zbhci_uart_init(DEVICE_DT_GET(DT_NODELABEL(uart1)));

	if (rc != 0) {
		LOG_ERR("ZBHCI UART init failed (%d)", rc);
	}
#endif

	LOG_INF("native_sim socket Zigbee %s ready",
		IS_ENABLED(CONFIG_ZIGBEE_COORDINATOR) ? "coordinator" :
		IS_ENABLED(CONFIG_ZIGBEE_ROUTER) ? "router" : "end device");
	return 0;
}
