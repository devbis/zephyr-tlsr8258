/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zigbee/zb_bootstrap.h>

LOG_MODULE_REGISTER(main);

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
	LOG_INF("zigbee_shell commissioning hook: deferred (Task4)");
#else
	LOG_INF("zigbee_shell commissioning hook: unavailable (BDB disabled)");
#endif
	return false;
}

void zb_platform_app_start_commissioning(void)
{
	LOG_INF("zigbee_shell commissioning start: no-op");
}

int main(void)
{
	LOG_INF("Zigbee shell starting on TLSR8258 TB03F");
	printk("Zigbee shell starting on TLSR8258 TB03F\n");
	LOG_INF("Waiting for Zigbee bootstrap callbacks");
	return 0;
}
