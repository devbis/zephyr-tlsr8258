/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_types.h>
#include "zb_common.h"
#include "drv_hw.h"
#include <zephyr/zigbee/zb_radio_port.h>
#include "drv_nv.h"
#include "ev_timer.h"
#include "mac/mac.h"
#include "mac/mac_trx.h"
#include "mac/mac_data.h"

LOG_MODULE_REGISTER(zigbee_drv_hw, CONFIG_ZIGBEE_LOG_LEVEL);

#if defined(CONFIG_ADC)
#if DT_NODE_EXISTS(DT_PATH(zephyr_user))
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
/* Battery-voltage channel: zephyr,user io-channels = <&adc0 N>. */
#define ZB_VBAT_ADC_SPEC 1
static const struct adc_dt_spec zb_vbat_adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
#endif
#endif
#endif

/* Voltage read in millivolts. Returns false if no reading could be taken. */
static bool zb_vbat_read_mv(uint16_t *mv)
{
#ifdef ZB_VBAT_ADC_SPEC
	struct adc_sequence seq;
	int16_t raw = 0;

	if (!adc_is_ready_dt(&zb_vbat_adc)) {
		return false;
	}
	adc_sequence_init_dt(&zb_vbat_adc, &seq);
	seq.buffer = &raw;
	seq.buffer_size = sizeof(raw);
	if (adc_read_dt(&zb_vbat_adc, &seq) != 0) {
		return false;
	}
	*mv = (uint16_t)raw;
	return true;
#else
	ARG_UNUSED(mv);
	return false;
#endif
}

uint8_t sys_exceptionPost(uint16_t line, uint8_t evt)
{
	LOG_WRN("Zigbee exception line=%u event=0x%02x", line, evt);
	return 0U;
}

int drv_hwTmr_set(uint8_t tmrIdx, uint32_t t_us, timerCb_t func, void *arg)
{
	ev_timer_event_t *evt;
	uint32_t timeout_ms;

	ARG_UNUSED(tmrIdx);
	if (func == NULL) {
		return -EINVAL;
	}

	/*
	 * The vendor CSMA timer is expressed in microseconds; the Zephyr EV
	 * timer backend is millisecond based. Round up so a non-zero backoff is
	 * never collapsed into an immediate callback.
	 */
	timeout_ms = (t_us + 999U) / 1000U;
	if (timeout_ms == 0U) {
		timeout_ms = 1U;
	}

	evt = ev_timer_taskPost(func, arg, timeout_ms);
	return (evt != NULL) ? 0 : -ENOMEM;
}

startup_state_e drv_platform_init(void)
{
	return SYSTEM_BOOT;
}

uint32_t drv_disable_irq(void)
{
	return irq_lock();
}

uint32_t drv_restore_irq(uint32_t en)
{
	irq_unlock(en);
	return en;
}

void drv_irqMask_clear(void)
{
}

void drv_wd_setInterval(uint32_t ms)
{
	ARG_UNUSED(ms);
}

void drv_wd_start(void)
{
}

void drv_wd_clear(void)
{
}

uint32_t drv_u32Rand(void)
{
	uint32_t val;

	sys_rand_get(&val, sizeof(val));
	return val;
}

void drv_generateRandomData(uint8_t *pData, uint8_t len)
{
	sys_rand_get(pData, len);
}

#define VOLTAGE_DEBOUNCE_NUM 5
/*
 * Vendor voltage_detect(). A persistent low reading is logged instead of
 * resetting or sleeping: the reset path is unverified on this port, and the
 * flash write guard already refuses writes below BATTERY_SAFETY_THRESHOLD.
 */
void voltage_detect(bool powerOn)
{
	uint16_t voltage;
	int32_t debounce = VOLTAGE_DEBOUNCE_NUM;
	int64_t start;

	if (!zb_vbat_read_mv(&voltage)) {
		return;
	}
	if (!powerOn && (voltage >= BATTERY_SAFETY_THRESHOLD)) {
		return;
	}

	start = k_uptime_get();
	while (debounce > 0) {
		if (voltage > BATTERY_SAFETY_THRESHOLD) {
			debounce--;
		} else {
			debounce = VOLTAGE_DEBOUNCE_NUM;
		}

		if ((k_uptime_get() - start) >= 1000) {
			LOG_ERR("VDD %u mV is below %u mV", voltage, BATTERY_SAFETY_THRESHOLD);
			return;
		}

		if (!zb_vbat_read_mv(&voltage)) {
			return;
		}
	}
}

/* Flash writes and erases need VDD above BATTERY_SAFETY_THRESHOLD (vendor flash_safe_voltage_set). */
bool zb_flash_vdd_ok(void)
{
#ifdef ZB_VBAT_ADC_SPEC
	uint16_t voltage;

	if (!zb_vbat_read_mv(&voltage)) {
		return false;
	}
	return voltage >= BATTERY_SAFETY_THRESHOLD;
#else
	/* No battery channel configured: nothing to check against. */
	return true;
#endif
}

static int zb_platform_hw_init(void)
{
	(void)drv_platform_init();

#ifdef ZB_VBAT_ADC_SPEC
	if (adc_channel_setup_dt(&zb_vbat_adc) == 0) {
		voltage_detect(true);
	} else {
		LOG_ERR("battery ADC channel setup failed");
	}
#endif

	return 0;
}

SYS_INIT(zb_platform_hw_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

void drv_vbusWatchdogClose(void)
{
}

bool drv_get_primary_ieee_addr(uint8_t *addr)
{
	int eui64_rc;
	ssize_t id_len;

	if (addr == NULL) {
		return false;
	}

	eui64_rc = hwinfo_get_device_eui64(addr);
	if (eui64_rc == 0) {
		LOG_HEXDUMP_INF(addr, 8, "hwinfo EUI64");
		return true;
	}

	id_len = hwinfo_get_device_id(addr, 8);
	if (id_len >= 8) {
		LOG_HEXDUMP_INF(addr, 8, "hwinfo device_id as IEEE");
		return true;
	}

	LOG_WRN("No primary IEEE address from hwinfo: eui64_rc=%d id_len=%d", eui64_rc,
		(int)id_len);
	return false;
}

uint32_t drv_pm_deepSleep_frameCnt_get(void)
{
	/*
	 * No retention register on this port: the security IB is reloaded from
	 * NV before the stack starts, so the value already in it is the right
	 * one. Returning anything lower would rewind the counter and make the
	 * trust centre drop our frames as replays.
	 */
	return ss_ib.outgoingFrameCounter;
}
