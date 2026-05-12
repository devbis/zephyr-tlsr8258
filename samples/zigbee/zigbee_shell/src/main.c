/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <stdint.h>

LOG_MODULE_REGISTER(main);

#if defined(CONFIG_ZIGBEE_BDB)
static bool commissioning_start_requested;
static bool bdb_runtime_ready;
#endif

void zb_platform_app_bootstrap_ready(void)
{
	LOG_INF("zigbee_shell bootstrap hook: core init complete");
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
	LOG_INF("Waiting for Zigbee bootstrap callbacks");
	return 0;
}
