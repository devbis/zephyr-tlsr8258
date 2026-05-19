/* SPDX-License-Identifier: Apache-2.0 */

#include "zb_common_stub.h"

#include <errno.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

LOG_MODULE_REGISTER(zigbee_persist, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZB_PERSIST_BLOB_VERSION 2U

typedef struct {
	u8 version;
	u8 reserved[3];
	zb_info_t zb_info;
	nwk_ctx_t nwk_ctx;
} zb_persist_blob_t;

static bool zb_persist_len_registered;

static void zb_persist_register_item_length(void)
{
	if (zb_persist_len_registered) {
		return;
	}

	(void)nv_itemLengthCheckAdd(NV_ITEM_ZB_INFO, sizeof(zb_persist_blob_t));
	zb_persist_len_registered = true;
}

int zb_platform_restore_persistent_state(void)
{
	zb_persist_blob_t blob;
	nv_sts_t st;

	zb_persist_register_item_length();
	memset(&blob, 0, sizeof(blob));

	st = nv_flashReadNew(1, NV_MODULE_ZB_INFO, NV_ITEM_ZB_INFO, sizeof(blob), (u8 *)&blob);
	if (st != NV_SUCC) {
		return -ENOENT;
	}
	if (blob.version != ZB_PERSIST_BLOB_VERSION) {
		return -EINVAL;
	}

	g_zbInfo = blob.zb_info;
	g_zbNwkCtx = blob.nwk_ctx;
	g_zbNwkCtx.is_factory_new = g_zbNwkCtx.joined ? 0U : 1U;

	return 0;
}

void zb_info_save(void *arg)
{
	zb_persist_blob_t blob;
	nv_sts_t st;

	ARG_UNUSED(arg);

	zb_persist_register_item_length();
	memset(&blob, 0, sizeof(blob));
	blob.version = ZB_PERSIST_BLOB_VERSION;
	blob.zb_info = g_zbInfo;
	blob.nwk_ctx = g_zbNwkCtx;
	blob.nwk_ctx.is_factory_new = blob.nwk_ctx.joined ? 0U : 1U;

	st = nv_flashWriteNew(1, NV_MODULE_ZB_INFO, NV_ITEM_ZB_INFO, sizeof(blob), (u8 *)&blob);
	if (st != NV_SUCC) {
		LOG_WRN("Zigbee persistence save failed: %d", st);
	}
}
