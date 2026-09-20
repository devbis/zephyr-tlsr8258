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

static void test_end_device_timeout_path_is_vendor_derived(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_endDev_timeout.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "int nwkEndDevWaitForTimeoutRspCb(void *arg)"));
	EXPECT_TRUE(contains(source, "void nwkEndDevTimeoutReqSend(reqTimeoutEnum_t reqTimeoutEnum"));
	EXPECT_TRUE(contains(source, "void nwkEndDevTimeoutRspCmdHandler(void *arg"));
	EXPECT_TRUE(contains(source, "void nwkEndDevTimeoutRejoin(void)"));
	free(source);
}

static void test_ed_poll_timeout_uses_zephyr_timer_adapter(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "int pollRateCb(void *arg)"));
	EXPECT_TRUE(contains(source, "ev_timer_taskPost(pollRateCb, NULL, rate)"));
	EXPECT_TRUE(contains(source, "zdo_set_pollRate(0)"));
	free(source);
}

int main(void)
{
	test_end_device_timeout_path_is_vendor_derived();
	test_ed_poll_timeout_uses_zephyr_timer_adapter();

	if (failures != 0) {
		fprintf(stderr, "zigbee_timeout_request_path: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_timeout_request_path: PASS\n");
	return 0;
}
