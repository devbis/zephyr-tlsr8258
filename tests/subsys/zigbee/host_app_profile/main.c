/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host-only unit tests for the zigbee_shell app_profile module.
 *
 * Verifies:
 *  - The authoritative endpoint/profile/device/cluster constants.
 *  - app_profile_register() wires zcl_init, af_endpointRegister, and
 *    zcl_register exactly once and with the expected arguments.
 *  - The authoritative manufacturer / model strings used by the fallback
 *    interview path in mac_trx_compat.c match the constants defined here.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- minimal stubs so app_profile.c compiles in the host environment ---- */

typedef unsigned char  u8;
typedef unsigned short u16;

/* ZCL data-type codes (mirrored from zcl_const.h) */
#define ZCL_DATA_TYPE_UINT8    0x20
#define ZCL_DATA_TYPE_ENUM8    0x30
#define ZCL_DATA_TYPE_CHAR_STR 0x42

/* Access-control bit (mirrored from zcl_const.h) */
#define ACCESS_CONTROL_READ    0x01

/* Cluster IDs (mirrored from zcl_const.h) */
#define ZCL_CLUSTER_GEN_BASIC    0x0000
#define ZCL_CLUSTER_GEN_IDENTIFY 0x0003

/* Global cluster revision (mirrored from zcl.h) */
#define ZCL_ATTRID_GLOBAL_CLUSTER_REVISION       0xFFFDU
#define ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT 0x0001U
#define ZCL_DATA_TYPE_UINT16   0x21
#define ZCL_DATA_TYPE_BOOLEAN  0x10
#define ZCL_DATA_TYPE_BITMAP8  0x18

/* Basic cluster attribute IDs (mirrored from zcl_basic.h) */
#define ZCL_ATTRID_BASIC_ZCL_VER      0x0000
#define ZCL_ATTRID_BASIC_APP_VER      0x0001
#define ZCL_ATTRID_BASIC_STACK_VER    0x0002
#define ZCL_ATTRID_BASIC_HW_VER       0x0003
#define ZCL_ATTRID_BASIC_MFR_NAME     0x0004
#define ZCL_ATTRID_BASIC_MODEL_ID     0x0005
#define ZCL_ATTRID_BASIC_POWER_SOURCE 0x0007

/* Identify cluster attribute IDs (mirrored from zcl_identify.h) */
#define ZCL_ATTRID_IDENTIFY_TIME      0x0000

/* Attribute info struct (mirrors zcl.h zclAttrInfo_t) */
typedef struct zclAttrInfo {
	u16 id;
	u8  type;
	u8  access;
	u8 *data;
} zclAttrInfo_t;

/* zcl_specClusterInfo_t (mirrors zcl.h) */
typedef void (*cluster_forAppCb_t)(void *p);
typedef int  (*cluster_cmdHdlr_t)(void *p);
typedef int  (*cluster_registerFunc_t)(u8 ep, u16 manu, u8 num, const zclAttrInfo_t *tbl, cluster_forAppCb_t cb);

typedef struct {
	u16 clusterId;
	u16 manuCode;
	u16 attrNum;
	const zclAttrInfo_t *attrTbl;
	cluster_registerFunc_t clusterRegisterFunc;
	cluster_forAppCb_t clusterAppCb;
} zcl_specClusterInfo_t;

/* af_simple_descriptor_t (mirrors zb_af.h) */
typedef struct {
	u16 app_profile_id;
	u16 app_dev_id;
	u8  endpoint;
	u8  app_dev_ver;
	u8  reserved;
	u8  app_in_cluster_count;
	u8  app_out_cluster_count;
	u16 *app_in_cluster_lst;
	u16 *app_out_cluster_lst;
} af_simple_descriptor_t;

typedef void (*af_endpoint_cb_t)(void *p);
typedef void (*af_dataCnf_cb_t)(void *p);
typedef void (*zcl_hookFn_t)(void *p);

/* ---- spy state for the stubs ---- */
static int zcl_init_calls;
static zcl_hookFn_t zcl_init_fn;
static int af_ep_reg_calls;
static u8  af_ep_reg_ep;
static af_simple_descriptor_t *af_ep_reg_desc;
static af_endpoint_cb_t af_ep_reg_rx;
static int zcl_register_calls;
static u8  zcl_register_ep;
static u8  zcl_register_cluster_num;

typedef int status_t;

void zcl_rx_handler(void *p)
{
	(void)p;
}

void zcl_init(zcl_hookFn_t fn)
{
	zcl_init_calls++;
	zcl_init_fn = fn;
}

af_simple_descriptor_t *af_simpleDescGet(u8 ep)
{
	(void)ep;
	return NULL;  /* force registration every call in tests */
}

bool af_endpointRegister(u8 ep, af_simple_descriptor_t *desc,
			 af_endpoint_cb_t rx_cb, af_dataCnf_cb_t cnf_cb)
{
	(void)cnf_cb;
	af_ep_reg_calls++;
	af_ep_reg_ep   = ep;
	af_ep_reg_desc = desc;
	af_ep_reg_rx   = rx_cb;
	return true;
}

void zcl_register(u8 endpoint, u8 cluster_num, zcl_specClusterInfo_t *info)
{
	(void)info;
	zcl_register_calls++;
	zcl_register_ep = endpoint;
	zcl_register_cluster_num = cluster_num;
}

status_t zcl_basic_register(u8 ep, u16 manu, u8 num,
			     const zclAttrInfo_t *tbl, cluster_forAppCb_t cb)
{
	(void)ep;
	(void)manu;
	(void)num;
	(void)tbl;
	(void)cb;
	return 0;
}

status_t zcl_identify_register(u8 ep, u16 manu, u8 num,
				const zclAttrInfo_t *tbl, cluster_forAppCb_t cb)
{
	(void)ep;
	(void)manu;
	(void)num;
	(void)tbl;
	(void)cb;
	return 0;
}

/* Minimal LOG stubs */
#define LOG_MODULE_REGISTER(name)
#define LOG_ERR(...)
#define LOG_WRN(...)
#define LOG_INF(...)

/* Declarations for hooks that app_profile.c defines (the test calls them
 * directly to verify the overrides work). */
void zb_platform_app_register_endpoints(void);
const char *zb_platform_app_basic_mfr_name(void);
const char *zb_platform_app_basic_model_id(void);

/* ---- include the module under test ---- */
#include "../../../../samples/zigbee/zigbee_shell/src/app_profile.c"

/* ---- test helpers ---- */
static int failures;

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		printf("FAIL %s:%d  expected true:  %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_EQ(actual, expected) do { \
	long long _a = (long long)(actual); \
	long long _e = (long long)(expected); \
	if (_a != _e) { \
		printf("FAIL %s:%d  %s=%lld  expected %lld\n", \
		       __FILE__, __LINE__, #actual, _a, _e); \
		failures++; \
	} \
} while (0)

#define EXPECT_STR_EQ(actual, expected) do { \
	if (strcmp((actual), (expected)) != 0) { \
		printf("FAIL %s:%d  \"%s\" != \"%s\"\n", \
		       __FILE__, __LINE__, (actual), (expected)); \
		failures++; \
	} \
} while (0)

static void reset_spy(void)
{
	zcl_init_calls        = 0;
	zcl_init_fn           = NULL;
	af_ep_reg_calls       = 0;
	af_ep_reg_ep          = 0;
	af_ep_reg_desc        = NULL;
	af_ep_reg_rx          = NULL;
	zcl_register_calls    = 0;
	zcl_register_ep       = 0;
	zcl_register_cluster_num = 0;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_profile_constants(void)
{
	EXPECT_EQ(APP_PROFILE_ENDPOINT,        0x01U);
	EXPECT_EQ(APP_PROFILE_HA_PROFILE_ID,   0x0104U);
	EXPECT_EQ(APP_PROFILE_HA_DEVICE_ID,    0x0000U);
}

static void test_cluster_list_contains_basic(void)
{
	bool found = false;
	const af_simple_descriptor_t *desc =
		(const af_simple_descriptor_t *)app_profile_get_simple_desc();

	for (int i = 0; i < desc->app_in_cluster_count; i++) {
		if (desc->app_in_cluster_lst[i] == ZCL_CLUSTER_GEN_BASIC) {
			found = true;
		}
	}
	EXPECT_TRUE(found);
}

static void test_cluster_list_contains_identify(void)
{
	bool found = false;
	const af_simple_descriptor_t *desc =
		(const af_simple_descriptor_t *)app_profile_get_simple_desc();

	for (int i = 0; i < desc->app_in_cluster_count; i++) {
		if (desc->app_in_cluster_lst[i] == ZCL_CLUSTER_GEN_IDENTIFY) {
			found = true;
		}
	}
	EXPECT_TRUE(found);
}

static void test_simple_desc_endpoint_matches_constant(void)
{
	const af_simple_descriptor_t *desc =
		(const af_simple_descriptor_t *)app_profile_get_simple_desc();

	EXPECT_EQ(desc->endpoint, APP_PROFILE_ENDPOINT);
	EXPECT_EQ(desc->app_profile_id, APP_PROFILE_HA_PROFILE_ID);
	EXPECT_EQ(desc->app_dev_id, APP_PROFILE_HA_DEVICE_ID);
}

static void test_mfr_name_is_authoritative(void)
{
	EXPECT_STR_EQ(app_profile_mfr_name(), APP_PROFILE_MFR_NAME);
}

static void test_model_id_is_authoritative(void)
{
	EXPECT_STR_EQ(app_profile_model_id(), APP_PROFILE_MODEL_ID);
}

static void test_model_id_no_longer_says_minimal(void)
{
	/* The old hardcoded string was "tlsr8258-minimal"; it must be gone. */
	EXPECT_TRUE(strstr(app_profile_model_id(), "minimal") == NULL);
}

static void test_register_calls_zcl_init_once(void)
{
	reset_spy();
	app_profile_register();
	EXPECT_EQ(zcl_init_calls, 1);
}

static void test_register_calls_af_endpointRegister_with_correct_ep(void)
{
	reset_spy();
	app_profile_register();
	EXPECT_EQ(af_ep_reg_calls, 1);
	EXPECT_EQ(af_ep_reg_ep, APP_PROFILE_ENDPOINT);
}

static void test_register_passes_zcl_rx_handler_as_rx_callback(void)
{
	reset_spy();
	app_profile_register();
	EXPECT_TRUE(af_ep_reg_rx == (af_endpoint_cb_t)zcl_rx_handler);
}

static void test_register_calls_zcl_register_with_two_clusters(void)
{
	reset_spy();
	app_profile_register();
	EXPECT_EQ(zcl_register_calls, 1);
	EXPECT_EQ(zcl_register_ep, APP_PROFILE_ENDPOINT);
	EXPECT_EQ(zcl_register_cluster_num, 2);
}

static void test_register_endpoints_hook_calls_app_profile_register(void)
{
	reset_spy();
	zb_platform_app_register_endpoints();
	EXPECT_EQ(zcl_init_calls, 1);
	EXPECT_EQ(af_ep_reg_calls, 1);
	EXPECT_EQ(zcl_register_calls, 1);
}

static void test_basic_mfr_name_hook_matches_constant(void)
{
	EXPECT_STR_EQ(zb_platform_app_basic_mfr_name(), APP_PROFILE_MFR_NAME);
}

static void test_basic_model_id_hook_matches_constant(void)
{
	EXPECT_STR_EQ(zb_platform_app_basic_model_id(), APP_PROFILE_MODEL_ID);
}

int main(void)
{
	test_profile_constants();
	test_cluster_list_contains_basic();
	test_cluster_list_contains_identify();
	test_simple_desc_endpoint_matches_constant();
	test_mfr_name_is_authoritative();
	test_model_id_is_authoritative();
	test_model_id_no_longer_says_minimal();
	test_register_calls_zcl_init_once();
	test_register_calls_af_endpointRegister_with_correct_ep();
	test_register_passes_zcl_rx_handler_as_rx_callback();
	test_register_calls_zcl_register_with_two_clusters();
	test_register_endpoints_hook_calls_app_profile_register();
	test_basic_mfr_name_hook_matches_constant();
	test_basic_model_id_hook_matches_constant();

	if (failures != 0) {
		printf("host_app_profile: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_app_profile: PASS\n");
	return 0;
}
