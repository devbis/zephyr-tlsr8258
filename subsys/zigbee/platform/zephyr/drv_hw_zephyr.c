/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_types.h>
#include "drv_hw.h"
#include "drv_nv.h"

LOG_MODULE_REGISTER(zigbee_drv_hw, CONFIG_ZIGBEE_LOG_LEVEL);

volatile int32_t zb_hwinfo_trace[4] = {
	0x48574945, 0, 0, 0,
};

u32 sysTimerPerUs = CONFIG_ZIGBEE_MAC_TIMER_CYCLES_PER_US;

startup_state_e drv_platform_init(void)
{
	return SYSTEM_BOOT;
}

void drv_enable_irq(void)
{
	irq_unlock(0);
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

void voltage_detect(bool powerOn)
{
	ARG_UNUSED(powerOn);
}

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

static int flash_area_for_addr(u32 addr, const struct flash_area **fa,
			       off_t *off, size_t *max_len)
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

#if FIXED_PARTITION_EXISTS(nvs_storage)
	if (fa->fa_id == DT_FIXED_PARTITION_ID(DT_NODELABEL(nvs_storage))) {
		return true;
	}
#endif
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
			LOG_ERR("flash_read: invalid addr 0x%08x len=%u", cur_addr, remaining);
			memset(dst, 0xFF, remaining);
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_read(fa, off, dst, chunk) < 0) {
			LOG_ERR("flash_read: backend failed addr=0x%08x len=%u", cur_addr, (u32)chunk);
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

	while (remaining > 0U) {
		const struct flash_area *fa;
		off_t off;
		size_t max_len;
		size_t chunk;

		if (flash_area_for_addr(cur_addr, &fa, &off, &max_len) < 0 || (max_len == 0U)) {
			LOG_ERR("flash_write: invalid addr 0x%08x len=%u", cur_addr, remaining);
			return;
		}
		if (!flash_area_write_allowed(fa)) {
			LOG_ERR("flash_write: denied partition id=%u addr=0x%08x", fa->fa_id, cur_addr);
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_write(fa, off, src, chunk) < 0) {
			LOG_ERR("flash_write: backend failed addr=0x%08x len=%u", cur_addr, (u32)chunk);
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

	while (remaining > 0U) {
		const struct flash_area *fa;
		off_t off;
		size_t max_len;
		size_t chunk;

		if (flash_area_for_addr(cur_addr, &fa, &off, &max_len) < 0 || (max_len == 0U)) {
			LOG_ERR("flash_erase: invalid addr 0x%08x len=%u", cur_addr, remaining);
			return;
		}
		if (!flash_area_write_allowed(fa)) {
			LOG_ERR("flash_erase: denied partition id=%u addr=0x%08x", fa->fa_id, cur_addr);
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_erase(fa, off, chunk) < 0) {
			LOG_ERR("flash_erase: backend failed addr=0x%08x len=%u", cur_addr, (u32)chunk);
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
		zb_hwinfo_trace[1] = -EINVAL;
		return false;
	}

	eui64_rc = hwinfo_get_device_eui64(addr);
	zb_hwinfo_trace[1] = eui64_rc;
	if (eui64_rc == 0) {
		zb_hwinfo_trace[2] = 1;
		zb_hwinfo_trace[3] = 8;
		LOG_HEXDUMP_INF(addr, 8, "hwinfo EUI64");
		return true;
	}

	id_len = hwinfo_get_device_id(addr, 8);
	zb_hwinfo_trace[2] = 2;
	zb_hwinfo_trace[3] = (int32_t)id_len;
	if (id_len >= 8) {
		LOG_HEXDUMP_INF(addr, 8, "hwinfo device_id as IEEE");
		return true;
	}

	LOG_WRN("No primary IEEE address from hwinfo: eui64_rc=%d id_len=%d",
		eui64_rc, (int)id_len);
	return false;
}
