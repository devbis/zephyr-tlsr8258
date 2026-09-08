#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static char *read_file(const char *path)
{
	FILE *fp = fopen(path, "rb");
	long size;
	char *buffer;

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

static bool contains(const char *source, const char *needle)
{
	return strstr(source, needle) != NULL;
}

static bool contains_between(const char *source, const char *start_marker,
				     const char *end_marker, const char *needle)
{
	const char *start = strstr(source, start_marker);
	const char *end;
	const char *match;

	if (start == NULL) {
		return false;
	}
	end = strstr(start, end_marker);
	match = strstr(start, needle);
	return end != NULL && match != NULL && match < end;
}

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_router_leave_request_uses_vendor_leave_path(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_leave.c");
	const char *func = "void tl_zbNwkNlmeLeaveRequestHandler(void *arg)";
	const char *next_func = "void tl_zbNwkLeaveReqCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "nwkLeaveReqStart(arg, NWK_BROADCAST_RX_ON_WHEN_IDLE, 0);"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "NWK_STATUS_INVALID_REQUEST"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "NWK_STATUS_UNKNOWN_DEVICE"));
	EXPECT_TRUE(contains(source, "nwkLeaveReqSend(void *arg"));
	free(source);
}

static void test_ed_abort_releases_native_zdo_state(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_libzigbee.c");
	const char *func = "void zb_ed_operation_abort(void)";
	const char *next_func = "void zb_ed_runtime_reset(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "zdo_nwkDiscoveryStop();"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "zdo_nwkRejoinWithBackOffStop();"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "zb_buf_free((zb_buf_t *)zdo_nwk_mngr()->savedBuf);"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_zbNwkCtx.user_state = NLME_IDLE;"));
	free(source);
}

static void test_ed_reset_uses_explicit_lifecycle_seam(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_libzigbee.c");
	const char *func = "void zb_ed_runtime_reset(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "zb_ed_operation_abort();"));
	EXPECT_TRUE(contains_between(source, func, "void zb_ed_fixed_join_target(",
				     "g_zbNwkCtx.is_factory_new = 1U;"));
	EXPECT_TRUE(contains_between(source, func, "void zb_ed_fixed_join_target(",
				     "aps_ib.aps_use_insecure_join = TRUE;"));
	free(source);
}

int main(void)
{
	test_router_leave_request_uses_vendor_leave_path();
	test_ed_abort_releases_native_zdo_state();
	test_ed_reset_uses_explicit_lifecycle_seam();

	if (failures != 0) {
		fprintf(stderr, "zigbee_nwk_leave_handling: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_nwk_leave_handling: PASS\n");
	return 0;
}
