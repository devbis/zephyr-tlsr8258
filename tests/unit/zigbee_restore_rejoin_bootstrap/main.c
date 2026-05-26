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

static void test_restore_joined_target_uses_rejoin_instead_of_direct_poll_resume(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/platform/zephyr/zb_bdb_bootstrap.c");
	const char *func = "static void zb_platform_bdb_restore_joined_target(void)";
	const char *next_func = "static void zb_platform_bdb_apply_fixed_target(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "bdb_outgoingFrameCountUpdate(1U);"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "zb_rejoinSecModeSet(REJOIN_SECURITY);"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "zdo_nwkRejoinStart((u32)1U << g_zbMacPib.phyChannelCur,"));
	EXPECT_FALSE(contains_between(source, func, next_func,
				      "tl_zbNwkEdMinimalPollRestart(zdo_af_get_syn_rate());"));

	free(source);
}

static void test_rejoin_security_mode_matches_vendor_semantics(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api_ed_compat.c");
	const char *func = "__attribute__((weak)) void zb_rejoinSecModeSet(u8 mode)";
	const char *next_func = "__attribute__((weak)) u8 zb_directJoinReq(u32 scanChannels, u8 scanDuration)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func,
				     next_func,
				     "aps_ib.aps_use_insecure_join = FALSE;"));
	EXPECT_TRUE(contains_between(source, func,
				     next_func,
				     "aps_ib.aps_authenticated = ss_ib.securityLevel ? TRUE : FALSE;"));
	EXPECT_TRUE(contains_between(source, func,
				     next_func,
				     "aps_ib.aps_use_insecure_join = TRUE;"));
	EXPECT_TRUE(contains_between(source, func,
				     next_func,
				     "aps_ib.aps_authenticated = FALSE;"));

	EXPECT_TRUE(contains_between(source,
				     "__attribute__((weak)) void bdb_outgoingFrameCountUpdate(u8 repower)",
				     func,
				     "ss_ib.outgoingFrameCounter += SS_UPDATE_FRAMECOUNT_THRES;"));
	EXPECT_TRUE(contains_between(source,
				     "__attribute__((weak)) void bdb_outgoingFrameCountUpdate(u8 repower)",
				     func,
				     "nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);"));

	free(source);
}

int main(void)
{
	test_restore_joined_target_uses_rejoin_instead_of_direct_poll_resume();
	test_rejoin_security_mode_matches_vendor_semantics();

	if (failures != 0) {
		fprintf(stderr, "zigbee_restore_rejoin_bootstrap: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_restore_rejoin_bootstrap: PASS\n");
	return 0;
}
