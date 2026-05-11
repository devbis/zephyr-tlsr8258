/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

int main(void)
{
	LOG_INF("Zigbee shell starting on TLSR8258 TB03F");
	/* Zigbee thread is started via K_THREAD_DEFINE in zb_main.c */
	return 0;
}
