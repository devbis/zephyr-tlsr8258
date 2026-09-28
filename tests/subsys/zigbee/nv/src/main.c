/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Zigbee NV backend on top of NVS, on the flash simulator partition of
 * native_sim.
 */

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(zigbee, CONFIG_ZIGBEE_LOG_LEVEL);

#include "drv_nv.h"

static void reset_nv(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_equal(nv_resetAll(), NV_SUCC);
}

ZTEST(zigbee_nv, test_module_reset_isolated)
{
	uint8_t ota_data[4] = {1, 2, 3, 4};
	uint8_t aps_data[4] = {5, 6, 7, 8};
	uint8_t readback[4] = {0};
	zassert_equal(
		nv_flashWriteNew(1, NV_MODULE_OTA, NV_ITEM_OTA_CODE, sizeof(ota_data), ota_data),
		NV_SUCC);
	zassert_equal(
		nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(aps_data), aps_data),
		NV_SUCC);

	zassert_equal(nv_resetModule(NV_MODULE_OTA), NV_SUCC);
	zassert_equal(
		nv_flashReadNew(1, NV_MODULE_OTA, NV_ITEM_OTA_CODE, sizeof(readback), readback),
		NV_ITEM_NOT_FOUND);
	zassert_equal(
		nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(readback), readback),
		NV_SUCC);
	zassert_true(memcmp(readback, aps_data, sizeof(readback)) == 0);
}

ZTEST(zigbee_nv, test_length_contract_rejects_short_registered_item)
{
	uint8_t stored_data[8] = {0};
	uint8_t readback[16] = {0};
	nv_itemLengthCheckAdd(NV_ITEM_ZCL_SCENE_TABLE, sizeof(readback));
	zassert_equal(nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_SCENE_TABLE,
				       sizeof(stored_data), stored_data),
		      NV_SUCC);
	zassert_equal(nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_SCENE_TABLE,
				      sizeof(stored_data), readback),
		      NV_DATA_CHECK_ERROR);
}

ZTEST(zigbee_nv, test_length_contract_rejects_short_requested_item)
{
	uint8_t stored_data[8] = {0};
	uint8_t readback[16] = {0};
	zassert_equal(nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_ON_OFF, sizeof(stored_data),
				       stored_data),
		      NV_SUCC);
	zassert_equal(
		nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_ON_OFF, sizeof(readback), readback),
		NV_DATA_CHECK_ERROR);
}

ZTEST(zigbee_nv, test_length_contract_rejects_registered_len_larger_than_request)
{
	uint8_t stored_data[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	uint8_t readback[16];
	uint8_t untouched[sizeof(readback)];
	memset(readback, 0xA5, sizeof(readback));
	memset(untouched, 0xA5, sizeof(untouched));
	nv_itemLengthCheckAdd(NV_ITEM_ZCL_SCENE_TABLE, sizeof(stored_data));
	zassert_equal(nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_SCENE_TABLE,
				       sizeof(stored_data), stored_data),
		      NV_SUCC);
	zassert_equal(nv_flashReadNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_SCENE_TABLE, 8, readback),
		      NV_DATA_CHECK_ERROR);
	zassert_true(memcmp(readback, untouched, sizeof(readback)) == 0);
}

ZTEST(zigbee_nv, test_read_by_index_does_not_alias_other_item)
{
	uint8_t value[4] = {9, 9, 9, 9};
	uint8_t readback[4] = {0};
	zassert_equal(
		nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, sizeof(value), value),
		NV_SUCC);
	zassert_equal(nv_flashReadByIndex(NV_MODULE_APS, NV_ITEM_APS_GROUP_TABLE, 0, 1,
					  sizeof(readback), readback),
		      NV_ITEM_NOT_FOUND);
}

ZTEST(zigbee_nv, test_delete_by_index_does_not_alias_other_item)
{
	uint8_t value[4] = {1, 3, 5, 7};
	uint8_t readback[4] = {0};
	zassert_equal(
		nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, sizeof(value), value),
		NV_SUCC);
	zassert_equal(nv_itemDeleteByIndex(NV_MODULE_APS, NV_ITEM_APS_GROUP_TABLE, 0, 1),
		      NV_ITEM_NOT_FOUND);
	zassert_equal(nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, sizeof(readback),
				      readback),
		      NV_SUCC);
	zassert_true(memcmp(readback, value, sizeof(readback)) == 0);
}

ZTEST(zigbee_nv, test_read_delete_by_index_zero_behaves_like_item)
{
	uint8_t value[4] = {10, 11, 12, 13};
	uint8_t readback[4] = {0};
	zassert_equal(
		nv_flashWriteNew(0, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, sizeof(value), value),
		NV_SUCC);
	zassert_equal(nv_flashReadByIndex(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, 0, 0,
					  sizeof(readback), readback),
		      NV_SUCC);
	zassert_true(memcmp(readback, value, sizeof(readback)) == 0);
	zassert_equal(nv_itemDeleteByIndex(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, 0, 0),
		      NV_SUCC);
	zassert_equal(nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, sizeof(readback),
				      readback),
		      NV_ITEM_NOT_FOUND);
}

ZTEST(zigbee_nv, test_indexed_records_keep_item_and_index_identity)
{
	uint8_t binding[] = {0x10, 0x11, 0x12};
	uint8_t group[] = {0x20, 0x21};
	uint8_t timeout[] = {0x30, 0x31, 0x32, 0x33};
	uint8_t readback[sizeof(timeout)] = {0};
	itemIfno_t info = {0, 0};
	zassert_equal(nv_flashWriteNew(0, NV_MODULE_ZB_INFO, NV_ITEM_ED_TIMEOUT, sizeof(binding),
				       binding),
		      NV_SUCC);
	zassert_equal(nv_flashWriteNew(0, NV_MODULE_ZB_INFO, NV_ITEM_APS_GROUP_TABLE, sizeof(group),
				       group),
		      NV_SUCC);
	zassert_equal(nv_flashWriteNew(0, NV_MODULE_ZB_INFO, NV_ITEM_ED_TIMEOUT, sizeof(timeout),
				       timeout),
		      NV_SUCC);

	/*
	 * ITEM_FIELD_IDLE is the vendor enumeration operation: return the
	 * latest physical indexed record, while preserving its item id/index.
	 */
	zassert_equal(nv_flashReadNew(0, NV_MODULE_ZB_INFO, ITEM_FIELD_IDLE, sizeof(info),
				      (uint8_t *)&info),
		      NV_SUCC);
	zassert_equal(info.opSect, 0);
	zassert_equal(info.opIndex, 2);

	zassert_equal(nv_flashReadByIndex(NV_MODULE_ZB_INFO, NV_ITEM_ED_TIMEOUT, info.opSect,
					  info.opIndex, sizeof(readback), readback),
		      NV_SUCC);
	zassert_true(memcmp(readback, timeout, sizeof(timeout)) == 0);

	memset(readback, 0, sizeof(readback));
	zassert_equal(nv_flashReadByIndex(NV_MODULE_ZB_INFO, NV_ITEM_APS_GROUP_TABLE, 0, 1,
					  sizeof(group), readback),
		      NV_SUCC);
	zassert_true(memcmp(readback, group, sizeof(group)) == 0);

	zassert_equal(nv_itemDeleteByIndex(NV_MODULE_ZB_INFO, NV_ITEM_APS_GROUP_TABLE, 0, 1),
		      NV_SUCC);
	memset(readback, 0, sizeof(readback));
	zassert_equal(nv_flashReadByIndex(NV_MODULE_ZB_INFO, NV_ITEM_ED_TIMEOUT, 0, 2,
					  sizeof(timeout), readback),
		      NV_SUCC);
	zassert_true(memcmp(readback, timeout, sizeof(timeout)) == 0);
	zassert_equal(nv_flashReadByIndex(NV_MODULE_ZB_INFO, NV_ITEM_APS_GROUP_TABLE, 0, 1,
					  sizeof(group), readback),
		      NV_ITEM_NOT_FOUND);
}

ZTEST_SUITE(zigbee_nv, NULL, NULL, reset_nv, NULL, NULL);
