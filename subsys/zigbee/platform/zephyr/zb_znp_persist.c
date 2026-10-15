/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Flash copy of the host-written NV items. The host keeps its own
 * configuration marks there (for example that the adapter was configured),
 * and expects them to survive a restart of the coordinator.
 *
 * Writes mask interrupts while the flash is busy, so a burst of host writes
 * is saved once, after it has been quiet for a while.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "zb_common.h"
#include "drv_nv.h"
#include "zb_znp_internal.h"

LOG_MODULE_DECLARE(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZNP_PERSIST_ITEM    0x70U
#define ZNP_PERSIST_QUIET_MS 2000U

static uint8_t blob[ZNP_NV_EXPORT_MAX];
static uint32_t saved_generation;
static uint32_t seen_generation;
static uint32_t changed_at;

bool znp_persist_load(void)
{
	u16 len = 0U;

	if (nv_flashSingleItemSizeGet(NV_MODULE_APP, ZNP_PERSIST_ITEM, &len) != NV_SUCC ||
	    len == 0U || len > sizeof(blob) ||
	    nv_flashReadNew(1, NV_MODULE_APP, ZNP_PERSIST_ITEM, len, blob) != NV_SUCC) {
		return false;
	}

	if (!znp_nv_import(blob, len)) {
		LOG_WRN("znp: saved NV items are unusable");
		znp_nv_reset();
		return false;
	}

	saved_generation = znp_nv_generation();
	seen_generation = saved_generation;
	return true;
}

void znp_persist_flush(void)
{
	size_t len = znp_nv_export(blob, sizeof(blob));
	nv_sts_t st;

	if (len == 0U) {
		LOG_WRN("znp: NV items do not fit the saved copy");
		return;
	}

	st = nv_flashWriteNew(1, NV_MODULE_APP, ZNP_PERSIST_ITEM, (u16)len, blob);
	if (st != NV_SUCC) {
		LOG_WRN("znp: saving the NV items failed (%d)", st);
	}
	saved_generation = znp_nv_generation();
	seen_generation = saved_generation;
}

void znp_persist_service(void)
{
	uint32_t generation = znp_nv_generation();

	if (generation == saved_generation) {
		return;
	}

	if (generation != seen_generation) {
		seen_generation = generation;
		changed_at = k_uptime_get_32();
		return;
	}

	if (k_uptime_get_32() - changed_at >= ZNP_PERSIST_QUIET_MS) {
		znp_persist_flush();
	}
}
