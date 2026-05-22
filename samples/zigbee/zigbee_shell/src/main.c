/* SPDX-License-Identifier: Apache-2.0 */
/*
 * main.c — Zephyr bootstrap hook surface for the zigbee_shell sample.
 *
 * This file implements only the zb_platform_app_* callbacks required by
 * the Zigbee platform bootstrap thread.  All commissioning / BDB policy
 * lives in app_bdb.c; all endpoint / profile / ZCL wiring lives in
 * app_profile.c.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_config.h>

#include "app_bdb.h"
#include "app_profile.h"

LOG_MODULE_REGISTER(main);

#if !defined(CONFIG_ZIGBEE_BDB)
static bool radio_validation_started;
static struct k_work_delayable radio_probe_work;

static void zigbee_shell_radio_probe(struct k_work *work)
{
	struct zb_platform_radio_diag_snapshot snapshot;
	int rc;

	ARG_UNUSED(work);

	rc = zb_platform_radio_start_on_channel(CONFIG_ZIGBEE_CHANNEL);
	if (rc < 0) {
		LOG_ERR("radio probe start failed (ch=%d rc=%d)", CONFIG_ZIGBEE_CHANNEL, rc);
		goto reschedule;
	}

	rc = zb_platform_radio_send_beacon_request();
	if (rc < 0) {
		LOG_ERR("radio probe beacon request failed (rc=%d)", rc);
	}

	if (zb_platform_radio_diag_get(&snapshot) == 0) {
		LOG_INF("radio diag: ch=%u started=%u tx=%u/%u fail=%u rx=%u drop=%u err=%u rssi=%d",
			snapshot.channel, snapshot.started, snapshot.tx_success,
			snapshot.tx_attempts, snapshot.tx_failures, snapshot.rx_accept_count,
			snapshot.rx_drop_count, snapshot.last_error,
			snapshot.last_rx_rssi_dbm);
	}

reschedule:
	(void)k_work_schedule(&radio_probe_work, K_SECONDS(1));
}
#endif

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
#if !defined(CONFIG_ZIGBEE_BDB)
	if (!radio_validation_started) {
		k_work_init_delayable(&radio_probe_work, zigbee_shell_radio_probe);
		radio_validation_started = true;
		(void)k_work_schedule(&radio_probe_work, K_MSEC(250));
		LOG_INF("zigbee_shell radio validation scheduled on channel %d",
			CONFIG_ZIGBEE_CHANNEL);
	}
#endif
	app_bdb_bootstrap_ready();
}

bool zb_platform_app_enable_radio_smoke_probe(void)
{
	/* Keep smoke probe as opt-in diagnostics only. */
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

void zb_platform_app_bdb_commissioning_status(uint8_t status, bool joinedNetwork)
{
	app_bdb_commissioning_status(status, joinedNetwork);
}

int main(void)
{
	return 0;
}

