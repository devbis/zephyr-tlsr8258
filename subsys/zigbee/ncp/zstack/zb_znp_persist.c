/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Flash copy of the host-written NV items. Two banks keep the last complete
 * copy available while the next copy is written.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

#include "zb_common.h"
#include "drv_nv.h"
#include "zb_znp_internal.h"

LOG_MODULE_DECLARE(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

/* Keep the banks clear of NV_MODULE_APP item IDs 0x2a..0x2d. */
#define ZNP_PERSIST_BANK0_BASE        0x30U
#define ZNP_PERSIST_BANK0_MANIFEST    0x90U
#define ZNP_PERSIST_BANK1_BASE        0x91U
#define ZNP_PERSIST_BANK1_MANIFEST    0xF1U
#define ZNP_PERSIST_MAGIC             0x5A4E5056U
#define ZNP_PERSIST_VERSION           1U
#define ZNP_PERSIST_HEADER_SIZE       10U
#define ZNP_PERSIST_ENTRY_SIZE        8U
#define ZNP_PERSIST_CRC_SIZE          4U
#define ZNP_PERSIST_MANIFEST_MAX \
	(ZNP_PERSIST_HEADER_SIZE + ZNP_NV_MAX_ITEMS * ZNP_PERSIST_ENTRY_SIZE + \
	 ZNP_PERSIST_CRC_SIZE)
#define ZNP_PERSIST_QUIET_MS          2000U
#define ZNP_PERSIST_NO_BANK           0xFFU

static uint8_t manifest[ZNP_PERSIST_MANIFEST_MAX];
static uint8_t active_bank = ZNP_PERSIST_NO_BANK;
/* Banks whose flash content is known to match their manifest */
static uint8_t trusted_banks;
static uint32_t active_sequence;
static uint32_t saved_generation;
static uint32_t seen_generation;
static uint32_t changed_at;

static uint8_t bank_base(uint8_t bank)
{
	return (bank == 0U) ? ZNP_PERSIST_BANK0_BASE : ZNP_PERSIST_BANK1_BASE;
}

static uint8_t bank_manifest(uint8_t bank)
{
	return (bank == 0U) ? ZNP_PERSIST_BANK0_MANIFEST : ZNP_PERSIST_BANK1_MANIFEST;
}

static void znp_persist_retry_later(void)
{
	seen_generation = znp_nv_generation();
	changed_at = k_uptime_get_32();
}

static bool znp_persist_manifest_read(uint8_t bank, uint32_t *sequence, uint8_t *count)
{
	u16 len = 0U;
	size_t expected_len;
	u8 item = bank_manifest(bank);

	if (nv_flashSingleItemSizeGet(NV_MODULE_APP, item, &len) != NV_SUCC ||
	    len < ZNP_PERSIST_HEADER_SIZE + ZNP_PERSIST_CRC_SIZE ||
	    len > sizeof(manifest) ||
	    nv_flashReadNew(1U, NV_MODULE_APP, item, len, manifest) != NV_SUCC) {
		return false;
	}

	*count = manifest[5];
	expected_len = ZNP_PERSIST_HEADER_SIZE + (size_t)*count * ZNP_PERSIST_ENTRY_SIZE +
		       ZNP_PERSIST_CRC_SIZE;
	if (znp_get_u32(manifest) != ZNP_PERSIST_MAGIC || manifest[4] != ZNP_PERSIST_VERSION ||
	    *count > ZNP_NV_MAX_ITEMS || len != expected_len ||
	    znp_get_u32(&manifest[len - ZNP_PERSIST_CRC_SIZE]) !=
		crc32_ieee(manifest, len - ZNP_PERSIST_CRC_SIZE)) {
		return false;
	}

	*sequence = znp_get_u32(&manifest[6]);
	return true;
}

static bool znp_persist_bank_load(uint8_t bank, uint32_t expected_sequence)
{
	uint32_t sequence;
	uint8_t count;
	size_t offset = ZNP_PERSIST_HEADER_SIZE;

	if (!znp_persist_manifest_read(bank, &sequence, &count) || sequence != expected_sequence) {
		return false;
	}

	znp_nv_reset();
	for (uint8_t i = 0U; i < count; i++) {
		uint16_t id = znp_get_u16(&manifest[offset]);
		uint16_t len = znp_get_u16(&manifest[offset + 2U]);
		uint32_t expected_crc = znp_get_u32(&manifest[offset + 4U]);
		uint8_t item = (uint8_t)(bank_base(bank) + i);
		u16 stored_len = 0U;
		uint8_t *data;

		offset += ZNP_PERSIST_ENTRY_SIZE;
		data = znp_nv_restore_item(id, len);
		if (data == NULL ||
		    nv_flashSingleItemSizeGet(NV_MODULE_APP, item, &stored_len) != NV_SUCC ||
		    stored_len != len ||
		    nv_flashReadNew(1U, NV_MODULE_APP, item, len, data) != NV_SUCC ||
		    crc32_ieee(data, len) != expected_crc) {
			znp_nv_reset();
			return false;
		}
	}

	active_bank = bank;
	active_sequence = sequence;
	trusted_banks |= BIT(bank);
	saved_generation = znp_nv_generation();
	seen_generation = saved_generation;
	return true;
}

bool znp_persist_load(void)
{
	uint32_t sequence[2] = {0U, 0U};
	uint8_t count;
	bool valid[2];
	uint8_t first;

	active_bank = ZNP_PERSIST_NO_BANK;
	active_sequence = 0U;
	valid[0] = znp_persist_manifest_read(0U, &sequence[0], &count);
	valid[1] = znp_persist_manifest_read(1U, &sequence[1], &count);
	/* New copies must outrank every manifest in flash, usable or not */
	if (valid[0] || valid[1]) {
		active_sequence = (valid[1] && (!valid[0] ||
				   (int32_t)(sequence[1] - sequence[0]) > 0)) ?
				  sequence[1] : sequence[0];
	}
	if (valid[0] || valid[1]) {
		first = (valid[1] &&
			 (!valid[0] || (int32_t)(sequence[1] - sequence[0]) > 0)) ? 1U : 0U;
		if (znp_persist_bank_load(first, sequence[first])) {
			return true;
		}
		if (valid[1U - first] && znp_persist_bank_load(1U - first, sequence[1U - first])) {
			LOG_WRN("znp: latest NV bank is unusable; restored the previous bank");
			return true;
		}
		LOG_WRN("znp: saved NV banks are unusable");
		znp_nv_reset();
		return false;
	}

	return false;
}

void znp_persist_flush(void)
{
	uint32_t generation = znp_nv_generation();
	uint32_t sequence = active_sequence + 1U;
	uint8_t bank = (active_bank == 0U) ? 1U : 0U;
	size_t count = znp_nv_persist_count();
	size_t offset = ZNP_PERSIST_HEADER_SIZE;
	size_t manifest_len;
	uint32_t old_sequence;
	uint8_t old_count = 0U;
	bool old_valid;
	nv_sts_t st;

	if (count > ZNP_NV_MAX_ITEMS) {
		LOG_WRN("znp: too many NV items to save");
		znp_persist_retry_later();
		return;
	}

	/*
	 * The bank still holds the copy before the last one. An item that the old manifest
	 * lists unchanged at the same index is already in place, and looking it up in the
	 * flash costs far more than comparing the CRC. The old manifest is overwritten entry
	 * by entry, each entry after it has been compared.
	 */
	old_valid = ((trusted_banks & BIT(bank)) != 0U) &&
		    znp_persist_manifest_read(bank, &old_sequence, &old_count);
	trusted_banks &= (uint8_t)~BIT(bank);

	(void)znp_put_u32(manifest, ZNP_PERSIST_MAGIC);
	manifest[4] = ZNP_PERSIST_VERSION;
	manifest[5] = (uint8_t)count;
	(void)znp_put_u32(&manifest[6], sequence);
	for (size_t i = 0U; i < count; i++) {
		uint16_t id;
		uint16_t len;
		const uint8_t *data;
		uint8_t item = (uint8_t)(bank_base(bank) + i);
		uint32_t crc;
		bool unchanged;

		if (!znp_nv_persist_get(i, &id, &len, &data) || len == 0U) {
			LOG_WRN("znp: could not read an NV item for saving");
			znp_persist_retry_later();
			return;
		}

		crc = crc32_ieee(data, len);
		unchanged = old_valid && i < old_count && znp_get_u16(&manifest[offset]) == id &&
			    znp_get_u16(&manifest[offset + 2U]) == len &&
			    znp_get_u32(&manifest[offset + 4U]) == crc;
		if (!unchanged) {
			st = nv_flashWriteNew(1U, NV_MODULE_APP, item, len, (uint8_t *)data);
			if (st != NV_SUCC) {
				LOG_WRN("znp: saving NV item 0x%04x failed (%d)", id, st);
				znp_persist_retry_later();
				return;
			}
		}

		(void)znp_put_u16(&manifest[offset], id);
		(void)znp_put_u16(&manifest[offset + 2U], len);
		(void)znp_put_u32(&manifest[offset + 4U], crc);
		offset += ZNP_PERSIST_ENTRY_SIZE;
	}

	manifest_len = offset + ZNP_PERSIST_CRC_SIZE;
	(void)znp_put_u32(&manifest[offset],
			   crc32_ieee(manifest, manifest_len - ZNP_PERSIST_CRC_SIZE));
	st = nv_flashWriteNew(1U, NV_MODULE_APP, bank_manifest(bank), (u16)manifest_len, manifest);
	if (st != NV_SUCC) {
		LOG_WRN("znp: saving the NV manifest failed (%d)", st);
		znp_persist_retry_later();
		return;
	}

	active_bank = bank;
	active_sequence = sequence;
	trusted_banks |= BIT(bank);
	saved_generation = generation;
	seen_generation = generation;
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
