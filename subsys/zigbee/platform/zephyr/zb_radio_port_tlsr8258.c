/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154/tlsr8258.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/init.h>
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

int zb_radio_port_get_ieee_addr(uint8_t ieee_addr[8])
{
	if (ieee_addr == NULL) {
		return -EINVAL;
	}

	return tlsr8258_radio_get_ieee_addr(ieee_addr);
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

/* MAC deadlines are in clock_time() units, the raw system tick. */
uint32_t sysTimerPerUs = TLSR8258_SYSTEM_TICK_CYCLES_PER_US;

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

static zb_radio_port_rx_sink_t zb_tlsr_rx_sink;

static int zb_radio_port_tlsr8258_rx(const struct tlsr8258_rx_frame_view *frame)
{
	const struct zb_radio_rx_frame_view view = {
		.dma = frame->dma,
		.len = frame->len,
		.rssi_dbm = frame->rssi_dbm,
	};

	if (zb_tlsr_rx_sink == NULL) {
		return -ENOSYS;
	}

	return zb_tlsr_rx_sink(&view);
}

void zb_radio_port_register_rx_sink(zb_radio_port_rx_sink_t sink)
{
	zb_tlsr_rx_sink = sink;
	tlsr8258_radio_register_rx_sink((sink != NULL) ? zb_radio_port_tlsr8258_rx : NULL);
	tlsr8258_radio_register_rx_notify((sink != NULL) ? zb_platform_wake : NULL);
}

/* Overrides the weak hook the Zigbee thread calls on every pass. */
void zb_platform_radio_rx_poll(void)
{
	tlsr8258_radio_rx_poll();
}

void zb_radio_port_update_filters(uint16_t pan_id, uint16_t short_addr,
				  const uint8_t *ieee_addr)
{
	tlsr8258_radio_update_filters(pan_id, short_addr, ieee_addr);
}
