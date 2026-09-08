/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>

static const struct device *const radio = DEVICE_DT_GET(DT_NODELABEL(zb));

volatile uint32_t tlsr_802154_driver_marker;
volatile uint32_t tlsr_802154_driver_caps;
volatile int32_t tlsr_802154_driver_result;

static FUNC_NORETURN void park(uint32_t marker)
{
	tlsr_802154_driver_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

int main(void)
{
	const struct ieee802154_radio_api *api;
	int ret;

	tlsr_802154_driver_marker = 0x82587000u;

	if (!device_is_ready(radio)) {
		park(0x8258e701u);
	}

	api = (const struct ieee802154_radio_api *)radio->api;
	tlsr_802154_driver_caps = api->get_capabilities(radio);

	ret = api->set_channel(radio, 11);
	if (ret != 0 && ret != -EALREADY) {
		tlsr_802154_driver_result = ret;
		park(0x8258e702u);
	}

	ret = api->start(radio);
	if (ret != 0 && ret != -EALREADY) {
		tlsr_802154_driver_result = ret;
		park(0x8258e703u);
	}

	ret = api->cca(radio);
	if (ret != 0 && ret != -EBUSY) {
		tlsr_802154_driver_result = ret;
		park(0x8258e704u);
	}

	ret = api->stop(radio);
	if (ret != 0 && ret != -EALREADY) {
		tlsr_802154_driver_result = ret;
		park(0x8258e705u);
	}

	tlsr_802154_driver_result = 0;
	park(0x82580000u);
}
