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
#include <zephyr/storage/flash_map.h>
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

#if defined(CONFIG_SOC_TLSR8258)
#include "clock_cal.h"
#endif

#if defined(CONFIG_ADC_TLSR8258)
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_tlsr8258.h>
#endif

#if FIXED_PARTITION_EXISTS(zigbee_nv_partition)
#define ZB_NV_PARTITION DT_NODELABEL(zigbee_nv_partition)
#elif FIXED_PARTITION_EXISTS(storage_partition)
#define ZB_NV_PARTITION DT_NODELABEL(storage_partition)
#else
#error "Zigbee requires a fixed partition labeled zigbee_nv_partition or storage_partition"
#endif

LOG_MODULE_REGISTER(zigbee_drv_hw, CONFIG_ZIGBEE_LOG_LEVEL);

#if defined(CONFIG_ADC_TLSR8258)
#if DT_NODE_EXISTS(DT_PATH(zephyr_user))
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
/* Battery-voltage channel: zephyr,user io-channels = <&adc0 N>. */
#define ZB_VBAT_ADC_SPEC 1
static const struct adc_dt_spec zb_vbat_adc = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
#endif
#endif
#endif

#if defined(CONFIG_SOC_TLSR8258)
/* Factory calibration sector: FACTORY_CFG_BASE_ADD + 0xC0, by flash size (drv_nv.h). */
#if DT_REG_SIZE(DT_CHOSEN(zephyr_flash)) >= (2UL * 1024UL * 1024UL)
#define ZB_FACTORY_CFG_BASE 0x1FE000u
#elif DT_REG_SIZE(DT_CHOSEN(zephyr_flash)) >= (1024UL * 1024UL)
#define ZB_FACTORY_CFG_BASE 0x0FE000u
#else
#define ZB_FACTORY_CFG_BASE 0x077000u
#endif
#define ZB_CFG_ADC_CALIBRATION (ZB_FACTORY_CFG_BASE + 0xC0u)
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

u8 sys_exceptionPost(u16 line, u8 evt)
{
	LOG_WRN("Zigbee exception line=%u event=0x%02x", line, evt);
	return 0U;
}

int drv_hwTmr_set(u8 tmrIdx, u32 t_us, timerCb_t func, void *arg)
{
	ev_timer_event_t *evt;
	u32 timeout_ms;

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

/*
 * Vendor adc_vref calibration (drv_calibration.c, TLSR8258 branch): two-point
 * values first, then the one-point value. Unprogrammed bytes (0xff) fail the
 * range checks, which keeps the driver defaults.
 */
#ifdef ZB_VBAT_ADC_SPEC
static void zb_calibrate_adc_vref(void)
{
	uint8_t cal[7] = {0};
	uint16_t vref;
	int8_t offset;

	flash_read(ZB_CFG_ADC_CALIBRATION, sizeof(cal), cal);

	if ((cal[4] <= 127u) && (((cal[6] << 8) + cal[5]) >= 47u) &&
	    (((cal[6] << 8) + cal[5]) <= 300u)) {
		vref = (uint16_t)((cal[6] << 8) + cal[5] + 1000u);
		offset = (int8_t)(cal[4] - 20);
	} else {
		vref = (uint16_t)(920u + cal[0] + cal[1]);
		if ((vref < 1047u) || (vref > 1302u)) {
			return;
		}
		offset = 0;
	}

	(void)adc_tlsr8258_set_calibration(zb_vbat_adc.dev, vref, offset);
}
#endif

startup_state_e drv_platform_init(void)
{
#if defined(CONFIG_SOC_TLSR8258)
	/* Vendor clock_init(): 24 MHz RC trim, on by default. */
	tlsr8258_rc_24m_cal();
	/* Vendor PM_CLOCK_INIT(): internal 32 kHz RC, trimmed. */
	tlsr8258_clock_32k_rc_init();
	tlsr8258_rc_32k_cal();
#ifdef ZB_VBAT_ADC_SPEC
	/* Vendor drv_calibration(): ADC reference. */
	zb_calibrate_adc_vref();
#endif
#endif

	return SYSTEM_BOOT;
}

u32 drv_disable_irq(void)
{
	return irq_lock();
}

u32 drv_restore_irq(u32 en)
{
	irq_unlock(en);
	return en;
}

void drv_irqMask_clear(void)
{
}

void drv_wd_setInterval(u32 ms)
{
	ARG_UNUSED(ms);
}

void drv_wd_start(void)
{
}

void drv_wd_clear(void)
{
}

u32 drv_u32Rand(void)
{
	u32 val;

	sys_rand_get(&val, sizeof(val));
	return val;
}

void drv_generateRandomData(u8 *pData, u8 len)
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
static bool zb_flash_vdd_ok(void)
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

struct flash_lookup_ctx {
	u32 addr;
	const struct flash_area *fa;
};

static void flash_area_find_cb(const struct flash_area *fa, void *user_data)
{
	struct flash_lookup_ctx *ctx = user_data;
	uint64_t area_start;
	uint64_t area_end;

	if (ctx->fa != NULL) {
		return;
	}

	area_start = (uint64_t)fa->fa_off;
	area_end = area_start + (uint64_t)fa->fa_size;

	if ((uint64_t)ctx->addr >= area_start && (uint64_t)ctx->addr < area_end) {
		ctx->fa = fa;
	}
}

static int flash_area_for_addr(u32 addr, const struct flash_area **fa, off_t *off, size_t *max_len)
{
	struct flash_lookup_ctx ctx = {
		.addr = addr,
		.fa = NULL,
	};
	size_t area_off;

	flash_area_foreach(flash_area_find_cb, &ctx);
	if ((ctx.fa == NULL) || !flash_area_device_is_ready(ctx.fa)) {
		return -ENODEV;
	}

	area_off = (size_t)((uint64_t)addr - (uint64_t)ctx.fa->fa_off);
	*fa = ctx.fa;
	*off = (off_t)area_off;
	*max_len = ctx.fa->fa_size - area_off;

	return 0;
}

static bool flash_area_write_allowed(const struct flash_area *fa)
{
	if (fa == NULL) {
		return false;
	}

	if (fa->fa_id == DT_FIXED_PARTITION_ID(ZB_NV_PARTITION)) {
		return true;
	}
#if FIXED_PARTITION_EXISTS(slot1_partition)
	if (fa->fa_id == DT_FIXED_PARTITION_ID(DT_NODELABEL(slot1_partition))) {
		return true;
	}
#endif

	return false;
}

void flash_read(u32 addr, u32 len, u8 *buf)
{
	u32 cur_addr = addr;
	u32 remaining = len;
	u8 *dst = buf;

	if ((buf == NULL) || (len == 0U)) {
		return;
	}

	while (remaining > 0U) {
		const struct flash_area *fa;
		off_t off;
		size_t max_len;
		size_t chunk;

		if (flash_area_for_addr(cur_addr, &fa, &off, &max_len) < 0 || (max_len == 0U)) {
			LOG_ERR("%s: invalid addr 0x%08x len=%u", __func__, cur_addr, remaining);
			memset(dst, 0xFF, remaining);
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_read(fa, off, dst, chunk) < 0) {
			LOG_ERR("%s: backend failed addr=0x%08x len=%u", __func__, cur_addr,
				(u32)chunk);
			memset(dst, 0xFF, remaining);
			return;
		}

		cur_addr += (u32)chunk;
		remaining -= (u32)chunk;
		dst += chunk;
	}
}

void flash_write(u32 addr, u32 len, u8 *buf)
{
	u32 cur_addr = addr;
	u32 remaining = len;
	u8 *src = buf;

	if ((buf == NULL) || (len == 0U)) {
		return;
	}
	if (!zb_flash_vdd_ok()) {
		LOG_ERR("%s: VDD below %u mV, write refused", __func__, BATTERY_SAFETY_THRESHOLD);
		return;
	}

	while (remaining > 0U) {
		const struct flash_area *fa;
		off_t off;
		size_t max_len;
		size_t chunk;

		if (flash_area_for_addr(cur_addr, &fa, &off, &max_len) < 0 || (max_len == 0U)) {
			LOG_ERR("%s: invalid addr 0x%08x len=%u", __func__, cur_addr, remaining);
			return;
		}
		if (!flash_area_write_allowed(fa)) {
			LOG_ERR("%s: denied partition id=%u addr=0x%08x", __func__, fa->fa_id,
				cur_addr);
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_write(fa, off, src, chunk) < 0) {
			LOG_ERR("%s: backend failed addr=0x%08x len=%u", __func__, cur_addr,
				(u32)chunk);
			return;
		}

		cur_addr += (u32)chunk;
		remaining -= (u32)chunk;
		src += chunk;
	}
}

void flash_erase(u32 addr)
{
	u32 cur_addr = addr;
	u32 remaining = FLASH_SECTOR_SIZE;

	if (!zb_flash_vdd_ok()) {
		LOG_ERR("%s: VDD below %u mV, erase refused", __func__, BATTERY_SAFETY_THRESHOLD);
		return;
	}

	while (remaining > 0U) {
		const struct flash_area *fa;
		off_t off;
		size_t max_len;
		size_t chunk;

		if (flash_area_for_addr(cur_addr, &fa, &off, &max_len) < 0 || (max_len == 0U)) {
			LOG_ERR("%s: invalid addr 0x%08x len=%u", __func__, cur_addr, remaining);
			return;
		}
		if (!flash_area_write_allowed(fa)) {
			LOG_ERR("%s: denied partition id=%u addr=0x%08x", __func__, fa->fa_id,
				cur_addr);
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_erase(fa, off, chunk) < 0) {
			LOG_ERR("%s: backend failed addr=0x%08x len=%u", __func__, cur_addr,
				(u32)chunk);
			return;
		}

		cur_addr += (u32)chunk;
		remaining -= (u32)chunk;
	}
}

bool drv_get_primary_ieee_addr(u8 *addr)
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

u32 drv_pm_deepSleep_frameCnt_get(void)
{
	/*
	 * No retention register on this port: the security IB is reloaded from
	 * NV before the stack starts, so the value already in it is the right
	 * one. Returning anything lower would rewind the counter and make the
	 * trust centre drop our frames as replays.
	 */
	return ss_ib.outgoingFrameCounter;
}
