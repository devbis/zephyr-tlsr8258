/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <stdint.h>

LOG_MODULE_REGISTER(main);

#if defined(CONFIG_ZIGBEE_BDB)
extern uint8_t bdb_networkSteerStart(void);
static bool commissioning_start_requested;
#endif

void zb_platform_app_bootstrap_ready(void)
{
	LOG_INF("zigbee_shell bootstrap hook: core init complete");
}

bool zb_platform_app_enable_radio_smoke_probe(void)
{
	/* Keep smoke probe as opt-in diagnostics only. */
	return false;
}

bool zb_platform_app_should_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
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

	if (commissioning_start_requested) {
		LOG_WRN("zigbee_shell commissioning start: already requested");
		return;
	}

	commissioning_start_requested = true;
	status = bdb_networkSteerStart();
	LOG_INF("zigbee_shell commissioning start requested (bdb status: 0x%02x)", status);
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
