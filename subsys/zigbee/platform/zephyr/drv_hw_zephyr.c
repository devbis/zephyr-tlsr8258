/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/random/random.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/zigbee/zb_types.h>
#include "drv_hw.h"
#include "drv_nv.h"

/* 24 MHz system clock → 24 ticks per microsecond */
u32 sysTimerPerUs = 24;

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
			memset(dst, 0xFF, remaining);
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_read(fa, off, dst, chunk) < 0) {
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
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_write(fa, off, src, chunk) < 0) {
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
			return;
		}

		chunk = (remaining < max_len) ? (size_t)remaining : max_len;
		if (flash_area_erase(fa, off, chunk) < 0) {
			return;
		}

		cur_addr += (u32)chunk;
		remaining -= (u32)chunk;
	}
}

bool drv_get_primary_ieee_addr(u8 *addr)
{
	ssize_t id_len;

	if (addr == NULL) {
		return false;
	}

	if (hwinfo_get_device_eui64(addr) == 0) {
		return true;
	}

	id_len = hwinfo_get_device_id(addr, 8);
	return id_len >= 8;
}
