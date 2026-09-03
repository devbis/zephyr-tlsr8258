#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static char *read_file(const char *path)
{
	FILE *fp;
	long size;
	char *buffer;

	fp = fopen(path, "rb");
	if (fp == NULL) {
		fprintf(stderr, "FAIL %s:%d: unable to open %s\n", __FILE__, __LINE__, path);
		failures++;
		return NULL;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return NULL;
	}

	size = ftell(fp);
	if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return NULL;
	}

	buffer = malloc((size_t)size + 1u);
	if (buffer == NULL) {
		fclose(fp);
		return NULL;
	}

	if (fread(buffer, 1u, (size_t)size, fp) != (size_t)size) {
		free(buffer);
		fclose(fp);
		return NULL;
	}

	buffer[size] = '\0';
	fclose(fp);
	return buffer;
}

static const char *slice_between(const char *source, const char *start_marker,
				 const char *end_marker)
{
	const char *start;
	const char *end;

	start = strstr(source, start_marker);
	if (start == NULL) {
		return NULL;
	}

	end = strstr(start, end_marker);
	if (end == NULL) {
		return NULL;
	}

	return start;
}

static bool contains(const char *source, const char *needle)
{
	return strstr(source, needle) != NULL;
}

static bool contains_between(const char *source, const char *start_marker,
			     const char *end_marker, const char *needle)
{
	const char *start;
	const char *end;
	const char *match;

	start = strstr(source, start_marker);
	if (start == NULL) {
		return false;
	}

	end = strstr(start, end_marker);
	if (end == NULL) {
		return false;
	}

	match = strstr(start, needle);
	return match != NULL && match < end;
}

static bool function_branch_contains(const char *source, const char *func_marker,
				     const char *branch_start, const char *branch_end,
				     const char *needle)
{
	const char *func;

	func = strstr(source, func_marker);
	if (func == NULL) {
		return false;
	}

	return contains_between(func, branch_start, branch_end, needle);
}

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		fprintf(stderr, "FAIL %s:%d expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_raw_mode_tclk_path_requests_key_from_nodedesc_handler(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/bdb/bdb.c");
	const char *func = "static void bdb_nodeDescRespHandler(void *arg)";
	const char *next_func = "static s32 bdb_retrieveTcLinkKeyTimeout(void *arg)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(slice_between(source, func, next_func) != NULL);
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "if (zb_apsmeRequestKeyReq(&requestKey) == RET_OK)"));
	EXPECT_FALSE(contains_between(source, func, next_func,
				      "if (!g_bdbCtx.edRuntimeReady)"));

	free(source);
}

static void test_raw_mode_tclk_path_starts_with_vendor_nodedesc_probe(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/bdb/bdb.c");
	const char *start_func =
		"static s32 bdb_retrieveTcLinkKeyStart(void *arg)";
	const char *timeout_func =
		"static s32 bdb_retrieveTcLinkKeyTimeout(void *arg)";
	const char *raw_if = "#if defined(CONFIG_IEEE802154_RAW_MODE)";
	const char *raw_else = "#else";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(function_branch_contains(source, start_func, raw_if, raw_else,
					     "zb_zdoNodeDescReq(0x0000, &req, &sn, bdb_nodeDescRespHandler);"));
	EXPECT_TRUE(function_branch_contains(source, timeout_func, raw_if, raw_else,
					     "zb_zdoNodeDescReq(0x0000, &req, &sn, bdb_nodeDescRespHandler);"));
	EXPECT_FALSE(function_branch_contains(source, start_func, raw_if, raw_else,
					      "if (aps_ib.aps_authenticated && ss_ib.securityLevel != 0U)"));
	EXPECT_FALSE(function_branch_contains(source, timeout_func, raw_if, raw_else,
					      "if (aps_ib.aps_authenticated && ss_ib.securityLevel != 0U)"));

	free(source);
}

static void test_native_polling_remains_owned_by_vendor_zdo_manager(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");
	const char *poll_func = "int pollRateCb(void *arg)";
	const char *set_func = "void zdo_set_pollRate(u32 rate)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, poll_func, set_func,
				     "zdo_syncReq(NULL);"));
	EXPECT_TRUE(contains_between(source, set_func, "void zdo_nwkRejoinWithBackOffStop(void)",
				     "ev_timer_taskPost(pollRateCb, NULL, rate);"));
	free(source);
}

static void test_security_mode_set_does_not_force_nwk_security_before_key_install(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "void zb_rejoinSecModeSet(u8 mode)"));
	EXPECT_TRUE(contains(source, "aps_ib.aps_use_insecure_join = FALSE;"));
	EXPECT_TRUE(contains(source, "aps_ib.aps_authenticated = ss_ib.securityLevel ? TRUE : FALSE;"));
	EXPECT_FALSE(contains(source, "__attribute__((weak))"));

	free(source);
}

int main(void)
{
	test_raw_mode_tclk_path_requests_key_from_nodedesc_handler();
	test_raw_mode_tclk_path_starts_with_vendor_nodedesc_probe();
	test_native_polling_remains_owned_by_vendor_zdo_manager();
	test_security_mode_set_does_not_force_nwk_security_before_key_install();

	if (failures != 0) {
		fprintf(stderr, "zigbee_bdb_tclk_request: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_bdb_tclk_request: PASS\n");
	return 0;
}
