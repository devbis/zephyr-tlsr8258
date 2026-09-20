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

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		fprintf(stderr, "FAIL %s:%d expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_rejoin_api_delegates_to_vendor_zdo(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains_between(source, "u8 zb_rejoinReq(u32 scan_channels",
				     "u8 zb_rejoinReqWithBackOff", "zdo_nwkRejoinStart"));
	EXPECT_TRUE(contains_between(source, "u8 zb_rejoinReqWithBackOff(u32 scan_channels",
				     "void zb_rejoinSecModeSet", "zdo_nwkRejoinWithBackOff"));
	EXPECT_FALSE(strstr(source, "__attribute__((weak))") != NULL);
	free(source);
}

static void test_ed_abort_is_the_only_platform_lifecycle_hook(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_libzigbee.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(strstr(source, "void zb_ed_operation_abort(void)") != NULL);
	EXPECT_TRUE(strstr(source, "void zb_ed_runtime_reset(void)") != NULL);
	EXPECT_TRUE(strstr(source, "void zb_ed_fixed_join_target(") != NULL);
	free(source);
}

int main(void)
{
	test_rejoin_api_delegates_to_vendor_zdo();
	test_ed_abort_is_the_only_platform_lifecycle_hook();

	if (failures != 0) {
		fprintf(stderr, "zigbee_secure_rejoin_path: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_secure_rejoin_path: PASS\n");
	return 0;
}
