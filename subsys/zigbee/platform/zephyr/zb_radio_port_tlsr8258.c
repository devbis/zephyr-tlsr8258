/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/zigbee/zb_radio_port.h>

#define TLSR8258_SYSTEM_TICK_REG 0x00800740u
#define TLSR8258_SYSTEM_TICK_CYCLES_PER_US \
	(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000U)

static int zb_radio_port_tlsr8258_get(const struct device **dev,
				      const struct ieee802154_radio_api **api)
{
	const struct device *radio = DEVICE_DT_GET(DT_NODELABEL(zb));

	if (radio == NULL) {
		return -ENODEV;
	}

	/*
	 * On TLSR8258, the .data section is copied from a flash LMA that lies
	 * beyond the boot-mirror window (LMA 0x18B90 > mirror ceiling 0xAFFF).
	 * The TC32 startup copy loop may silently produce wrong data, leaving
	 * device_state.init_res stale from a previous firmware image.
	 * device_is_ready() therefore returns false even though the device is
	 * functional.
	 *
	 * Workaround: if device_is_ready() fails, force-initialise the device
	 * (device_init is a no-op if already done) and fall through to the
	 * api-pointer check which is the actual gate we care about.
	 */
	if (!device_is_ready(radio)) {
		if (!radio->state->initialized) {
			(void)device_init(radio);
		}
		if (radio->api == NULL) {
			return -ENODEV;
		}
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
	return sys_read32(TLSR8258_SYSTEM_TICK_REG);
}

bool zb_radio_port_clock_time_exceed(uint32_t ref_us, uint32_t span_us)
{
	return (uint32_t)(zb_radio_port_clock_time_us() - ref_us) >
	       (span_us * TLSR8258_SYSTEM_TICK_CYCLES_PER_US);
}

uint32_t zb_radio_port_clock_delta_to_us(uint32_t delta_us)
{
	return delta_us / TLSR8258_SYSTEM_TICK_CYCLES_PER_US;
}

void zb_radio_port_register_rx_sink(zb_radio_port_rx_sink_t sink)
{
	tlsr8258_zigbee_register_rx_sink(sink);
}

void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr)
{
	tlsr8258_zigbee_update_filters(pan_id, short_addr, ieee_addr);
}
