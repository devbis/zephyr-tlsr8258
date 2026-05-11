/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

int main(void)
{
	LOG_INF("Zigbee shell starting on TLSR8258 TB03F");
	LOG_INF("Radio smoke runs in Zigbee thread");
	return 0;
}
