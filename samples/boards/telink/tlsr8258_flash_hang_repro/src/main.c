/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/irq.h>
#include <tlsr825x/irq.h>

#define REPRO_PARTITION_ID DT_FIXED_PARTITION_ID(DT_NODELABEL(nvs_storage))

#define REPRO_MODE_DIRECT            1
#define REPRO_MODE_NVS               2
#define REPRO_MODE_NVS_IDLE_LOOP     3

struct repro_trace {
	uint32_t marker;
	uint32_t mode;
	uint32_t heartbeats;
	uint32_t write_attempts;
	uint32_t write_successes;
	uint32_t last_phase;
	uint32_t last_cycle;
	uint32_t last_delta;
	uint32_t irq_enabled;
	uint32_t irq_lock_depth;
	uint32_t irq_lock_max_depth;
	uint32_t last_offset;
	uint32_t last_status;
};

volatile struct repro_trace tlsr8258_flash_hang_repro_trace;

static struct nvs_fs repro_nvs;
static const struct flash_area *repro_area;
static uint8_t repro_buf[256];
static uint32_t repro_seq;
static uint32_t repro_next_ms;

static void repro_trace_update_phase(uint32_t phase)
{
	tlsr8258_flash_hang_repro_trace.last_phase = phase;
	tlsr8258_flash_hang_repro_trace.irq_enabled = arch_cpu_irqs_are_enabled() ? 1U : 0U;
	tlsr8258_flash_hang_repro_trace.irq_lock_depth = z_tc32_irq_lock_depth;
	tlsr8258_flash_hang_repro_trace.irq_lock_max_depth = z_tc32_irq_lock_max_depth;
}

static int repro_mode(void)
{
#if defined(CONFIG_TLSR8258_FLASH_HANG_REPRO_MODE_NVS_IDLE_LOOP)
	return REPRO_MODE_NVS_IDLE_LOOP;
#elif defined(CONFIG_TLSR8258_FLASH_HANG_REPRO_MODE_NVS)
	return REPRO_MODE_NVS;
#else
	return REPRO_MODE_DIRECT;
#endif
}

static int repro_flash_area_init(void)
{
	int rc = flash_area_open(REPRO_PARTITION_ID, &repro_area);

	if (rc < 0) {
		return rc;
	}
	if (repro_area == NULL || repro_area->fa_dev == NULL ||
	    !device_is_ready(repro_area->fa_dev)) {
		return -ENODEV;
	}

	return 0;
}

static int repro_nvs_init(void)
{
	struct flash_pages_info page_info;
	int rc;

	repro_nvs.flash_device = repro_area->fa_dev;
	repro_nvs.offset = repro_area->fa_off;

	rc = flash_get_page_info_by_offs(repro_nvs.flash_device, repro_nvs.offset, &page_info);
	if (rc < 0 || page_info.size == 0U) {
		return (rc < 0) ? rc : -EINVAL;
	}

	repro_nvs.sector_size = page_info.size;
	repro_nvs.sector_count = repro_area->fa_size / page_info.size;
	if (repro_nvs.sector_count == 0U) {
		return -EINVAL;
	}

	return nvs_mount(&repro_nvs);
}

static int repro_prepare_payload(void)
{
	memset(repro_buf, 0, sizeof(repro_buf));
	memcpy(repro_buf, "tlsr8258_flash_hang_repro", 25U);
	memcpy(&repro_buf[32], &repro_seq, sizeof(repro_seq));
	return 0;
}

static int repro_direct_write(void)
{
	off_t offset = (off_t)((repro_seq % 2U) * (repro_area->fa_size / 2U));
	size_t len = MIN(sizeof(repro_buf), repro_area->fa_size / 2U);
	off_t abs_off = repro_area->fa_off + offset;
	int rc;

	tlsr8258_flash_hang_repro_trace.last_offset = (uint32_t)offset;
	repro_trace_update_phase(0xD0010001U);
	rc = flash_erase(repro_area->fa_dev, abs_off, repro_area->fa_size / 2U);
	if (rc < 0) {
		tlsr8258_flash_hang_repro_trace.last_status = (uint32_t)rc;
		return rc;
	}

	repro_trace_update_phase(0xD0010002U);
	rc = flash_write(repro_area->fa_dev, abs_off, repro_buf, len);
	tlsr8258_flash_hang_repro_trace.last_status = (uint32_t)rc;
	return rc;
}

static int repro_nvs_write(void)
{
	int rc;
	uint16_t id = (uint16_t)(0x1000U + (repro_seq & 0xffU));

	tlsr8258_flash_hang_repro_trace.last_offset = id;
	repro_trace_update_phase(0xD0020001U);
	rc = nvs_write(&repro_nvs, id, repro_buf, sizeof(repro_buf));
	tlsr8258_flash_hang_repro_trace.last_status = (uint32_t)rc;
	return (rc < 0) ? rc : 0;
}

int main(void)
{
	uint32_t last_cycle = k_cycle_get_32();
	int mode = repro_mode();
	int rc;

	tlsr8258_flash_hang_repro_trace.marker = 0xF1A5E000U;
	tlsr8258_flash_hang_repro_trace.mode = (uint32_t)mode;
	repro_trace_update_phase(0xD0000001U);

	rc = repro_flash_area_init();
	if (rc < 0) {
		tlsr8258_flash_hang_repro_trace.last_status = (uint32_t)rc;
		for (;;) {
			compiler_barrier();
		}
	}

	if (mode != REPRO_MODE_DIRECT) {
		rc = repro_nvs_init();
		if (rc < 0) {
			tlsr8258_flash_hang_repro_trace.last_status = (uint32_t)rc;
			for (;;) {
				compiler_barrier();
			}
		}
	}

	repro_next_ms = k_uptime_get_32();
	while (1) {
		uint32_t now_ms = k_uptime_get_32();
		uint32_t now_cycle = k_cycle_get_32();

		tlsr8258_flash_hang_repro_trace.heartbeats++;
		tlsr8258_flash_hang_repro_trace.last_cycle = now_cycle;
		tlsr8258_flash_hang_repro_trace.last_delta = now_cycle - last_cycle;
		last_cycle = now_cycle;
		repro_trace_update_phase(0xD0000002U);

		if ((int32_t)(now_ms - repro_next_ms) >= 0) {
			repro_prepare_payload();
			tlsr8258_flash_hang_repro_trace.write_attempts++;
			repro_trace_update_phase(0xD0000003U);

			if (mode == REPRO_MODE_DIRECT) {
				rc = repro_direct_write();
			} else {
				rc = repro_nvs_write();
			}

			if (rc >= 0) {
				tlsr8258_flash_hang_repro_trace.write_successes++;
			}
			repro_seq++;
			repro_next_ms += CONFIG_TLSR8258_FLASH_HANG_REPRO_PERIOD_MS;
			repro_trace_update_phase(0xD0000004U);
		}

		if (mode == REPRO_MODE_NVS_IDLE_LOOP) {
			k_busy_wait(CONFIG_TLSR8258_FLASH_HANG_REPRO_IDLE_US);
		} else {
			k_busy_wait(1000U);
		}
	}
}
