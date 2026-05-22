/* SPDX-License-Identifier: Apache-2.0 */
/*
 * app_profile.c — ZCL endpoint registration for the zigbee_shell sample.
 *
 * Owns the authoritative endpoint descriptor, Basic/Identify attribute
 * tables, and the single app_profile_register() entry point that wires
 * the ZCL and AF layers together.
 *
 * Reference pattern: tl_zigbee_sdk/apps/sampleSwitch/sampleSwitch.c
 *   zcl_init(hook)
 *   af_endpointRegister(ep, &desc, zcl_rx_handler, NULL)
 *   zcl_register(ep, N, clusters)
 */

#include "app_profile.h"

/* In a real Zephyr build pull in the ZCL/AF SDK headers.
 * In the host-test harness the test file provides all stubs before
 * including this translation unit, so no SDK headers are needed. */
#if defined(__ZEPHYR__)
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_profile);
/*
 * Pull in SDK compiler/type shims before ZCL/AF headers.
 * compiler_zephyr.h defines _attribute_packed_ -> __packed so that
 * the SDK's "typedef struct _attribute_packed_ {...}" pattern produces
 * anonymous packed structs instead of a struct named _attribute_packed_.
 * zb_types.h provides u8/u16/u32 which ZCL/AF headers use but do not
 * include themselves.
 * af/zb_af.h defines epInfo_t which zcl.h uses in function declarations.
 */
#include "platform/zephyr/compiler_zephyr.h"
#include <zephyr/zigbee/zb_types.h>
#include "af/zb_af.h"
#include "zcl/zcl_const.h"
#include "zcl/zcl.h"
#include "zcl/general/zcl_basic.h"
#include "zcl/general/zcl_identify.h"
void zcl_rx_handler(void *p);
#endif /* __ZEPHYR__ */

/* ------------------------------------------------------------------ */
/* Basic cluster attribute table                                       */
/* ------------------------------------------------------------------ */

/* ZCL CHAR_STR encoding: first byte is length, then the characters.  */
static const u8 app_basic_zcl_ver    = 3U;
static const u8 app_basic_app_ver    = 1U;
static const u8 app_basic_stack_ver  = 2U;
static const u8 app_basic_hw_ver     = 1U;
static const u8 app_basic_power_src  = 0x03U; /* Battery (0x03) */
static const u16 app_basic_cluster_rev = ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT;

/* Manufacturer name and model ID as ZCL CHAR_STR (length-prefixed). */
static const u8 app_basic_mfr_name[] = {
	sizeof(APP_PROFILE_MFR_NAME) - 1U,
	'T', 'e', 'l', 'i', 'n', 'k',
};

static const u8 app_basic_model_id[] = {
	sizeof(APP_PROFILE_MODEL_ID) - 1U,
	't', 'l', 's', 'r', '8', '2', '5', '8', '-',
	'z', 'i', 'g', 'b', 'e', 'e', '-', 's', 'h', 'e', 'l', 'l',
};

static const zclAttrInfo_t app_basic_attr_tbl[] = {
	{ ZCL_ATTRID_BASIC_ZCL_VER,    ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,
	  (u8 *)&app_basic_zcl_ver },
	{ ZCL_ATTRID_BASIC_APP_VER,    ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,
	  (u8 *)&app_basic_app_ver },
	{ ZCL_ATTRID_BASIC_STACK_VER,  ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,
	  (u8 *)&app_basic_stack_ver },
	{ ZCL_ATTRID_BASIC_HW_VER,     ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,
	  (u8 *)&app_basic_hw_ver },
	{ ZCL_ATTRID_BASIC_MFR_NAME,   ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,
	  (u8 *)app_basic_mfr_name },
	{ ZCL_ATTRID_BASIC_MODEL_ID,   ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,
	  (u8 *)app_basic_model_id },
	{ ZCL_ATTRID_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8,  ACCESS_CONTROL_READ,
	  (u8 *)&app_basic_power_src },
	{ ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	  (u8 *)&app_basic_cluster_rev },
};

#define APP_BASIC_ATTR_NUM  ((u8)(sizeof(app_basic_attr_tbl) / sizeof(app_basic_attr_tbl[0])))

/* ------------------------------------------------------------------ */
/* Identify cluster attribute table                                    */
/* ------------------------------------------------------------------ */

static u16 app_identify_time;
static const u16 app_identify_cluster_rev = ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT;

static const zclAttrInfo_t app_identify_attr_tbl[] = {
	{ ZCL_ATTRID_IDENTIFY_TIME,           ZCL_DATA_TYPE_UINT16,  ACCESS_CONTROL_READ,
	  (u8 *)&app_identify_time },
	{ ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,  ACCESS_CONTROL_READ,
	  (u8 *)&app_identify_cluster_rev },
};

#define APP_IDENTIFY_ATTR_NUM  ((u8)(sizeof(app_identify_attr_tbl) / sizeof(app_identify_attr_tbl[0])))

/* ------------------------------------------------------------------ */
/* In-cluster list and simple descriptor                               */
/* ------------------------------------------------------------------ */

static u16 app_in_clusters[] = {
	ZCL_CLUSTER_GEN_BASIC,
	ZCL_CLUSTER_GEN_IDENTIFY,
};

static af_simple_descriptor_t app_simple_desc = {
	.app_profile_id     = APP_PROFILE_HA_PROFILE_ID,
	.app_dev_id         = APP_PROFILE_HA_DEVICE_ID,
	.endpoint           = APP_PROFILE_ENDPOINT,
	.app_dev_ver        = 1U,
	.reserved           = 0U,
	.app_in_cluster_count  = 2U,
	.app_out_cluster_count = 0U,
	.app_in_cluster_lst    = app_in_clusters,
	.app_out_cluster_lst   = NULL,
};

/* ------------------------------------------------------------------ */
/* ZCL cluster info table (for zcl_register)                          */
/* ------------------------------------------------------------------ */

static zcl_specClusterInfo_t app_cluster_info[] = {
	{
		.clusterId          = ZCL_CLUSTER_GEN_BASIC,
		.manuCode           = 0U,
		.attrNum            = APP_BASIC_ATTR_NUM,
		.attrTbl            = app_basic_attr_tbl,
		.clusterRegisterFunc = zcl_basic_register,
		.clusterAppCb       = NULL,
	},
	{
		.clusterId          = ZCL_CLUSTER_GEN_IDENTIFY,
		.manuCode           = 0U,
		.attrNum            = APP_IDENTIFY_ATTR_NUM,
		.attrTbl            = app_identify_attr_tbl,
		.clusterRegisterFunc = zcl_identify_register,
		.clusterAppCb       = NULL,
	},
};

#define APP_CLUSTER_NUM  ((u8)(sizeof(app_cluster_info) / sizeof(app_cluster_info[0])))

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void app_profile_register(void)
{
	zcl_init(NULL);

	if (af_simpleDescGet(APP_PROFILE_ENDPOINT) == NULL) {
		if (!af_endpointRegister(APP_PROFILE_ENDPOINT, &app_simple_desc,
					 (af_endpoint_cb_t)zcl_rx_handler, NULL)) {
			LOG_WRN("app_profile: endpoint %u register failed",
				APP_PROFILE_ENDPOINT);
		}
	}

	zcl_register(APP_PROFILE_ENDPOINT, APP_CLUSTER_NUM, app_cluster_info);
}

/*
 * Override the weak stub in zb_bdb_bootstrap.c so that BDB init
 * picks up our real ZCL-wired endpoint descriptor.
 */
void zb_platform_app_register_endpoints(void)
{
	app_profile_register();
}

/*
 * Override the weak identity stubs in mac_trx_compat.c so the fallback
 * interview path answers with the same strings as the real ZCL attribute
 * table.  The strings are plain C strings (not ZCL length-prefixed).
 */
const char *zb_platform_app_basic_mfr_name(void)
{
	return APP_PROFILE_MFR_NAME;
}

const char *zb_platform_app_basic_model_id(void)
{
	return APP_PROFILE_MODEL_ID;
}

const void *app_profile_get_simple_desc(void)
{
	return &app_simple_desc;
}

const char *app_profile_mfr_name(void)
{
	return APP_PROFILE_MFR_NAME;
}

const char *app_profile_model_id(void)
{
	return APP_PROFILE_MODEL_ID;
}
