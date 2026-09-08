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
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buffer = malloc((size_t)size + 1u);
	if (buffer == NULL || fread(buffer, 1u, (size_t)size, fp) != (size_t)size) {
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

static void test_interview_polling_is_native_zdo_behavior(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "int pollRateCb(void *arg)"));
	EXPECT_TRUE(contains(source, "zdo_syncReq(NULL);"));
	EXPECT_TRUE(contains(source, "zdo_set_pollRate(500U);"));
	free(source);
}

static void test_zdp_interview_uses_registered_vendor_endpoint(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/platform/zephyr/zb_primitive_dispatch.c");
	char *zdo = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_zephyr_glue.c");

	EXPECT_TRUE(source != NULL);
	EXPECT_TRUE(zdo != NULL);
	if (source == NULL || zdo == NULL) {
		free(source);
		free(zdo);
		return;
	}
	EXPECT_TRUE(contains(source, "void af_aps_data_entry(void *arg)"));
	EXPECT_TRUE(contains(source, "zdo_descriptorsIndicate(arg);"));
	EXPECT_TRUE(contains(source, "zdo_activeEpIndicate(arg);"));
	EXPECT_TRUE(contains(source, "zdo_ep->cb_rx(arg);"));
	EXPECT_TRUE(contains(zdo, "void zdp_init(void)"));
	EXPECT_TRUE(contains(zdo, "af_endpointRegister(ZDO_EP"));
	free(source);
	free(zdo);
}

static void test_fixed_join_is_an_explicit_ed_platform_seam(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_libzigbee.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "void zb_ed_fixed_join_target("));
	EXPECT_TRUE(contains(source, "tl_zbMacChannelSet(channel);"));
	EXPECT_TRUE(contains(source, "zb_preConfigNwkKey((u8 *)nwk_key, FALSE);"));
	free(source);
}

int main(void)
{
	test_interview_polling_is_native_zdo_behavior();
	test_zdp_interview_uses_registered_vendor_endpoint();
	test_fixed_join_is_an_explicit_ed_platform_seam();

	if (failures != 0) {
		fprintf(stderr, "zigbee_interview_poll_start: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_interview_poll_start: PASS\n");
	return 0;
}
