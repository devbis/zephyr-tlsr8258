/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zigbee/zb_bootstrap.h>

LOG_MODULE_REGISTER(main);

#if defined(CONFIG_ZIGBEE_BDB)
static bool commissioning_start_requested;
static bool bdb_runtime_ready;

static const uint8_t zigbee_shell_fixed_ext_pan_id[8] = {
	59, 9, 157, 6, 79, 143, 238, 112,
};

static const uint8_t zigbee_shell_fixed_network_key[16] = {
	76, 228, 73, 183, 178, 113, 243, 139,
	176, 183, 186, 153, 50, 177, 238, 220,
};
#else
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
#if !defined(CONFIG_ZIGBEE_BDB)
	ARG_UNUSED(target);
	return false;
#else
	if (target == NULL) {
		return false;
	}

	memset(target, 0, sizeof(*target));
	target->channel = CONFIG_ZIGBEE_CHANNEL;
	target->pan_id = 23335U;
	target->short_addr = 0x0000U;
	memcpy(target->ext_pan_id, zigbee_shell_fixed_ext_pan_id,
	       sizeof(zigbee_shell_fixed_ext_pan_id));
	memcpy(target->network_key, zigbee_shell_fixed_network_key,
	       sizeof(zigbee_shell_fixed_network_key));
	target->tc_addr_valid = false;

	return true;
#endif
}

void zb_platform_app_bootstrap_ready(void)
{
	LOG_INF("zigbee_shell bootstrap hook: core init complete");
#if !defined(CONFIG_ZIGBEE_BDB)
	if (!radio_validation_started) {
		k_work_init_delayable(&radio_probe_work, zigbee_shell_radio_probe);
		radio_validation_started = true;
		(void)k_work_schedule(&radio_probe_work, K_MSEC(250));
		LOG_INF("zigbee_shell radio validation scheduled on channel %d",
			CONFIG_ZIGBEE_CHANNEL);
	}
#endif
#if defined(CONFIG_ZIGBEE_BDB)
	if (!bdb_runtime_ready) {
		int err = zb_platform_bdb_init_default();

		if (err == 0) {
			bdb_runtime_ready = true;
			LOG_INF("zigbee_shell BDB runtime initialized");
		} else {
			LOG_ERR("zigbee_shell BDB runtime init failed (%d)", err);
		}
	}
#endif
}

bool zb_platform_app_enable_radio_smoke_probe(void)
{
	/* Keep smoke probe as opt-in diagnostics only. */
	return false;
}

bool zb_platform_app_should_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	if (!bdb_runtime_ready) {
		LOG_INF("zigbee_shell commissioning hook: BDB init pending");
		return false;
	}

	LOG_INF("zigbee_shell commissioning hook: BDB enabled");
	return true;
#else
	LOG_INF("zigbee_shell commissioning hook: unavailable (BDB disabled)");
	return false;
#endif
}

void zb_platform_app_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	uint8_t status;

	if (!bdb_runtime_ready) {
		LOG_WRN("zigbee_shell commissioning start: BDB runtime not initialized");
		return;
	}

	if (commissioning_start_requested) {
		LOG_WRN("zigbee_shell commissioning start: already requested");
		return;
	}

	status = zb_platform_bdb_network_steer_start();
	if (status == 0U) {
		commissioning_start_requested = true;
		LOG_INF("zigbee_shell commissioning start requested (bdb status: 0x%02x)", status);
	} else {
		LOG_WRN("zigbee_shell commissioning start rejected (bdb status: 0x%02x)", status);
	}
#else
	LOG_INF("zigbee_shell commissioning start: unavailable (BDB disabled)");
#endif
}

int main(void)
{
	LOG_INF("Zigbee shell starting on TLSR8258 TB03F");
	printk("Zigbee shell starting on TLSR8258 TB03F\n");
	LOG_INF("Waiting for Zigbee bootstrap callbacks (default channel %d)",
		CONFIG_ZIGBEE_CHANNEL);
	return 0;
}
