/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/zigbee/zb_radio_port.h>

static zb_radio_port_rx_sink_t g_rx_sink;

int zb_radio_port_native_sim_socket_register_rx_frame(const struct zb_radio_rx_frame_view *frame)
{
	if (g_rx_sink == NULL) {
		return -ENOSYS;
	}

	return g_rx_sink(frame);
}

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
	g_rx_sink = sink;
}

void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr)
{
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	struct ieee802154_filter filter;

	if (zb_radio_port_radio_get(&dev, &api) < 0 || api->filter == NULL) {
		return;
	}

	filter.pan_id = pan_id;
	(void)api->filter(dev, true, IEEE802154_FILTER_TYPE_PAN_ID, &filter);

	filter.short_addr = short_addr;
	(void)api->filter(dev, true, IEEE802154_FILTER_TYPE_SHORT_ADDR, &filter);

	if (ieee_addr != NULL) {
		filter.ieee_addr = ieee_addr;
		(void)api->filter(dev, true, IEEE802154_FILTER_TYPE_IEEE_ADDR, &filter);
	}
}
