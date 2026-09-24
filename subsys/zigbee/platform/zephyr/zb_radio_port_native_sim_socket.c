/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_REGISTER(zigbee_radio_port, CONFIG_ZIGBEE_LOG_LEVEL);

int zb_radio_port_radio_get(const struct device **dev,
			    const struct ieee802154_radio_api **api)
{
	const struct device *radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));

	if (radio == NULL || !device_is_ready(radio) || radio->api == NULL) {
		return -ENODEV;
	}

	if (dev != NULL) {
		*dev = radio;
	}
	if (api != NULL) {
		*api = (const struct ieee802154_radio_api *)radio->api;
	}

	return 0;
}

int zb_radio_port_set_channel(uint8_t channel)
{
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	int rc;

	rc = zb_radio_port_radio_get(&dev, &api);
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

	rc = zb_radio_port_radio_get(&dev, &api);
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
	return (uint32_t)k_ticks_to_us_floor32(k_uptime_ticks());
}

bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us)
{
	return (uint32_t)(zb_radio_port_clock_time_us() - ref_us) > span_us;
}

uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us)
{
	return delta_us;
}

void zb_radio_port_register_rx_sink(zb_radio_port_rx_sink_t sink)
{
	zb_radio_l2_register_rx_sink(sink);
}

void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr)
{
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	struct ieee802154_filter filter;
	int rc = zb_radio_port_radio_get(&dev, &api);

	if (rc < 0) {
		LOG_ERR("Cannot configure IEEE 802.15.4 filters: radio unavailable (%d)", rc);
		return;
	}
	if (api->filter == NULL) {
		LOG_ERR("IEEE 802.15.4 radio does not provide filter operation");
		return;
	}

	filter.pan_id = pan_id;
	rc = api->filter(dev, true, IEEE802154_FILTER_TYPE_PAN_ID, &filter);
	if (rc < 0) {
		LOG_ERR("IEEE 802.15.4 PAN filter failed (%d)", rc);
	}

	filter.short_addr = short_addr;
	rc = api->filter(dev, true, IEEE802154_FILTER_TYPE_SHORT_ADDR, &filter);
	if (rc < 0) {
		LOG_ERR("IEEE 802.15.4 short address filter failed (%d)", rc);
	}

	if (ieee_addr != NULL) {
		filter.ieee_addr = (uint8_t *)ieee_addr;
		rc = api->filter(dev, true, IEEE802154_FILTER_TYPE_IEEE_ADDR, &filter);
		if (rc < 0) {
			LOG_ERR("IEEE 802.15.4 IEEE address filter failed (%d)", rc);
		}
	}
}

void zb_radio_port_idle_rx_guard(void)
{
}

void zb_radio_port_watchdog_disable(void)
{
}

void zb_radio_port_watchdog_init(void)
{
}

void zb_radio_port_watchdog_feed(void)
{
}
