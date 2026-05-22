/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/zigbee/zb_radio_port.h>

static int zb_radio_port_tlsr8258_get(const struct device **dev,
				      const struct ieee802154_radio_api **api)
{
	const struct device *radio = DEVICE_DT_GET(DT_NODELABEL(zb));

	if (!device_is_ready(radio)) {
		return -ENODEV;
	}

	if (dev != NULL) {
		*dev = radio;
	}
	if (api != NULL) {
		*api = (const struct ieee802154_radio_api *)radio->api;
	}

	return (radio->api != NULL) ? 0 : -ENOSYS;
}

int zb_radio_port_radio_get(const struct device **dev,
			    const struct ieee802154_radio_api **api)
{
	return zb_radio_port_tlsr8258_get(dev, api);
}

int zb_radio_port_set_channel(uint8_t channel)
{
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	int rc;

	rc = zb_radio_port_tlsr8258_get(&dev, &api);
	if (rc < 0) {
		return rc;
	}
	if (api->set_channel == NULL) {
		return -ENOTSUP;
	}

	rc = api->set_channel(dev, channel);
	return (rc == -EALREADY) ? 0 : rc;
}

int zb_radio_port_set_trx_state(enum zb_radio_port_trx_state state,
				uint8_t channel)
{
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	int rc;

	rc = zb_radio_port_tlsr8258_get(&dev, &api);
	if (rc < 0) {
		return rc;
	}

	if (state == ZB_RADIO_PORT_TRX_OFF) {
		if (api->stop == NULL) {
			return -ENOTSUP;
		}
		rc = api->stop(dev);
		return (rc == -EALREADY) ? 0 : rc;
	}

	rc = zb_radio_port_set_channel(channel);
	if (rc < 0) {
		return rc;
	}
	if (api->start == NULL) {
		return -ENOTSUP;
	}

	rc = api->start(dev);
	return (rc == -EALREADY) ? 0 : rc;
}

uint32_t zb_radio_port_clock_time_us(void)
{
	return k_cyc_to_us_floor32(k_cycle_get_32());
}

bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us)
{
	return (uint32_t)(zb_radio_port_clock_time_us() - ref_us) >= span_us;
}

uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us)
{
	return delta_us;
}

void zb_radio_port_register_rx_cb(zb_radio_port_rx_cb_t cb)
{
	tlsr8258_zigbee_register_rx_cb((tlsr8258_zigbee_rx_cb_t)cb);
}

void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr)
{
	tlsr8258_zigbee_update_filters(pan_id, short_addr, ieee_addr);
}
