/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
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

#include <string.h>

/*
 * In a real Zephyr build pull in the ZCL/AF SDK headers.
 * In the host-test harness the test file provides all stubs before
 * including this translation unit, so no SDK headers are needed.
 */
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
#if defined(CONFIG_ZIGBEE_GP)
/*
 * gp/gp.h needs SEC_KEY_LEN (zb_config.h) and the gpSwitchInfo_t/
 * zcl_gp_*Cmd_t types (zcl_greenPower.h) that the piecemeal ZCL includes
 * above do not pull in; gp.c itself gets them via the full zcl_include.h.
 */
#include <zephyr/zigbee/zb_config.h>
#include "zcl/general/zcl_greenPower.h"
#include "gp/gp.h"
#endif
#if defined(CONFIG_ZIGBEE_OTA)
#include "ota/ota.h"
#endif
#if defined(CONFIG_ZIGBEE_WWAH)
#include "wwah/wwah.h"
#endif
#if defined(CONFIG_ZIGBEE_ZCL_ON_OFF)
#include <zephyr/drivers/gpio.h>
#include "zcl/general/zcl_onoff.h"
#endif
#endif /* __ZEPHYR__ */

/* ------------------------------------------------------------------ */
/* Basic cluster attribute table                                       */
/* ------------------------------------------------------------------ */

/* ZCL CHAR_STR encoding: first byte is length, then the characters.  */
static const u8 app_basic_zcl_ver = 3U;
static const u8 app_basic_app_ver = 1U;
static const u8 app_basic_stack_ver = 2U;
static const u8 app_basic_hw_ver = 1U;
static const u8 app_basic_power_src = 0x03U; /* Battery (0x03) */
static const u16 app_basic_cluster_rev = ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT;

/* Manufacturer name and model ID as ZCL CHAR_STR (length-prefixed). */
static u8 app_basic_mfr_name[1U + sizeof(APP_PROFILE_MFR_NAME)] = {
	sizeof(APP_PROFILE_MFR_NAME) - 1U,
};

static u8 app_basic_model_id[1U + sizeof(APP_PROFILE_MODEL_ID)] = {
	sizeof(APP_PROFILE_MODEL_ID) - 1U,
};

static const zclAttrInfo_t app_basic_attr_tbl[] = {
	{ZCL_ATTRID_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ,
	 (u8 *)&app_basic_zcl_ver},
	{ZCL_ATTRID_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ,
	 (u8 *)&app_basic_app_ver},
	{ZCL_ATTRID_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ,
	 (u8 *)&app_basic_stack_ver},
	{ZCL_ATTRID_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ,
	 (u8 *)&app_basic_hw_ver},
	{ZCL_ATTRID_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,
	 (u8 *)app_basic_mfr_name},
	{ZCL_ATTRID_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,
	 (u8 *)app_basic_model_id},
	{ZCL_ATTRID_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ,
	 (u8 *)&app_basic_power_src},
	{ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&app_basic_cluster_rev},
};

#define APP_BASIC_ATTR_NUM ((u8)ARRAY_SIZE(app_basic_attr_tbl))

/* ------------------------------------------------------------------ */
/* Identify cluster attribute table                                    */
/* ------------------------------------------------------------------ */

static u16 app_identify_time;
static const u16 app_identify_cluster_rev = ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT;

static const zclAttrInfo_t app_identify_attr_tbl[] = {
	{ZCL_ATTRID_IDENTIFY_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&app_identify_time},
	{ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&app_identify_cluster_rev},
};

#define APP_IDENTIFY_ATTR_NUM                                                                      \
	((u8)ARRAY_SIZE(app_identify_attr_tbl))

#if defined(CONFIG_ZIGBEE_ZCL_ON_OFF)
/* ------------------------------------------------------------------ */
/* On/Off cluster: drives the board's led0                             */
/* ------------------------------------------------------------------ */

static const struct gpio_dt_spec app_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static bool app_onoff_state;
static const u16 app_onoff_cluster_rev = ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT;

static const zclAttrInfo_t app_onoff_attr_tbl[] = {
	{ZCL_ATTRID_ONOFF, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE,
	 (u8 *)&app_onoff_state},
	{ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&app_onoff_cluster_rev},
};

#define APP_ONOFF_ATTR_NUM ((u8)ARRAY_SIZE(app_onoff_attr_tbl))

static void app_onoff_set(bool on)
{
	app_onoff_state = on;
	gpio_pin_set_dt(&app_led, on ? 1 : 0);
	zcl_setAttrVal(APP_PROFILE_ENDPOINT, ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ONOFF,
		       (u8 *)&app_onoff_state);
}

static status_t app_onoff_cmd(zclIncomingAddrInfo_t *addr_info, u8 cmd_id, void *payload)
{
	ARG_UNUSED(addr_info);
	ARG_UNUSED(payload);

	switch (cmd_id) {
	case ZCL_CMD_ONOFF_ON:
		app_onoff_set(true);
		break;
	case ZCL_CMD_ONOFF_OFF:
		app_onoff_set(false);
		break;
	case ZCL_CMD_ONOFF_TOGGLE:
		app_onoff_set(!app_onoff_state);
		break;
	default:
		return ZCL_STA_UNSUP_CLUSTER_COMMAND;
	}

	return ZCL_STA_SUCCESS;
}
#endif /* CONFIG_ZIGBEE_ZCL_ON_OFF */

/* ------------------------------------------------------------------ */
/* In-cluster list and simple descriptor                               */
/* ------------------------------------------------------------------ */

static u16 app_in_clusters[] = {
	ZCL_CLUSTER_GEN_BASIC,
	ZCL_CLUSTER_GEN_IDENTIFY,
#if defined(CONFIG_ZIGBEE_ZCL_ON_OFF)
	ZCL_CLUSTER_GEN_ON_OFF,
#endif
#if defined(CONFIG_ZIGBEE_WWAH)
	ZCL_CLUSTER_WWAH,
#endif
};

#if defined(CONFIG_ZIGBEE_OTA)
static u16 app_out_clusters[] = {
	ZCL_CLUSTER_OTA,
};
#endif

static af_simple_descriptor_t app_simple_desc = {
	.app_profile_id = APP_PROFILE_HA_PROFILE_ID,
	.app_dev_id = APP_PROFILE_HA_DEVICE_ID,
	.endpoint = APP_PROFILE_ENDPOINT,
	.app_dev_ver = 1U,
	.reserved = 0U,
	.app_in_cluster_count = ARRAY_SIZE(app_in_clusters),
#if defined(CONFIG_ZIGBEE_OTA)
	.app_out_cluster_count = 1U,
	.app_in_cluster_lst = app_in_clusters,
	.app_out_cluster_lst = app_out_clusters,
#else
	.app_out_cluster_count = 0U,
	.app_in_cluster_lst = app_in_clusters,
	.app_out_cluster_lst = NULL,
#endif
};

#if defined(CONFIG_ZIGBEE_OTA)
/*
 * Identity the server matches the OTA file against: raise file_ver with
 * every release, and keep image_type and manufacturer_code equal to the OTA
 * file header. totalImageSize is unused by the client.
 */
static ota_preamble_t app_ota_preamble = {
	.fileVer = 0x00000001U,
	.totalImageSize = 0U,
	.imageType = 0x0001U,
	.manufacturerCode = 0x1141U,
};

static void app_ota_event(u8 evt, u8 status)
{
	if ((evt == OTA_EVT_COMPLETE) && (status == ZCL_STA_SUCCESS)) {
		LOG_INF("OTA image downloaded, rebooting into MCUboot");
		ota_mcuReboot();
	}
}

static ota_callBack_t app_ota_cb = {
	.processMsgCbFunc = app_ota_event,
};
#endif

/* ------------------------------------------------------------------ */
/* ZCL cluster info table (for zcl_register)                          */
/* ------------------------------------------------------------------ */

static zcl_specClusterInfo_t app_cluster_info[] = {
	{
		.clusterId = ZCL_CLUSTER_GEN_BASIC,
		.manuCode = 0U,
		.attrNum = APP_BASIC_ATTR_NUM,
		.attrTbl = app_basic_attr_tbl,
		.clusterRegisterFunc = zcl_basic_register,
		.clusterAppCb = NULL,
	},
	{
		.clusterId = ZCL_CLUSTER_GEN_IDENTIFY,
		.manuCode = 0U,
		.attrNum = APP_IDENTIFY_ATTR_NUM,
		.attrTbl = app_identify_attr_tbl,
		.clusterRegisterFunc = zcl_identify_register,
		.clusterAppCb = NULL,
	},
#if defined(CONFIG_ZIGBEE_ZCL_ON_OFF)
	{
		.clusterId = ZCL_CLUSTER_GEN_ON_OFF,
		.manuCode = 0U,
		.attrNum = APP_ONOFF_ATTR_NUM,
		.attrTbl = app_onoff_attr_tbl,
		.clusterRegisterFunc = zcl_onOff_register,
		.clusterAppCb = app_onoff_cmd,
	},
#endif
};

#define APP_CLUSTER_NUM ((u8)ARRAY_SIZE(app_cluster_info))

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void app_profile_register(void)
{
	memcpy(&app_basic_mfr_name[1], APP_PROFILE_MFR_NAME, sizeof(APP_PROFILE_MFR_NAME));
	memcpy(&app_basic_model_id[1], APP_PROFILE_MODEL_ID, sizeof(APP_PROFILE_MODEL_ID));

#if defined(CONFIG_ZIGBEE_ZCL_ON_OFF)
	if (gpio_is_ready_dt(&app_led)) {
		gpio_pin_configure_dt(&app_led, GPIO_OUTPUT_INACTIVE);
	}
#endif

	zcl_init(NULL);

	if (af_simpleDescGet(APP_PROFILE_ENDPOINT) == NULL) {
		if (!af_endpointRegister(APP_PROFILE_ENDPOINT, &app_simple_desc,
					 (af_endpoint_cb_t)zcl_rx_handler, NULL)) {
			LOG_WRN("app_profile: endpoint %u register failed", APP_PROFILE_ENDPOINT);
		}
	}

	zcl_register(APP_PROFILE_ENDPOINT, APP_CLUSTER_NUM, app_cluster_info);

#if defined(CONFIG_ZIGBEE_WWAH)
	wwah_init(WWAH_TYPE_SERVER, &app_simple_desc);
#endif

#if defined(CONFIG_ZIGBEE_OTA)
	ota_init(OTA_TYPE_CLIENT, &app_simple_desc, &app_ota_preamble, &app_ota_cb);
	ota_queryStart(OTA_PERIODIC_QUERY_INTERVAL);
#endif
}

/*
 * Override the optional hook in zb_bdb_bootstrap.c so that BDB init
 * picks up our real ZCL-wired endpoint descriptor.
 */
void zb_platform_app_register_endpoints(void)
{
#if !defined(CONFIG_ZIGBEE_NCP)
	/* An NCP host registers the endpoints it serves. */
	app_profile_register();
#endif

#if defined(CONFIG_ZIGBEE_GP)
	/*
	 * Vendor ordering (tl_zigbee_sdk apps/sampleGW/sampleGateway.c):
	 * gp_init() runs after the app's own endpoint/cluster registration.
	 */
	gp_init(APP_PROFILE_ENDPOINT);
#endif
}

/*
 * Provide the identity hooks used by the Basic-cluster interview fallback,
 * keeping the answers consistent with the real ZCL attribute table. The
 * strings are plain C strings (not ZCL length-prefixed).
 */
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
