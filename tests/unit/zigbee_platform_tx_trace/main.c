/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
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
		fprintf(stderr, "FAIL %s:%d: unable to open %s\n",
			__FILE__, __LINE__, path);
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
	const char *start = strstr(source, start_marker);
	const char *end;
	const char *match;

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
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", \
			__FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_platform_tx_trace_is_recorded_for_beaconreq_and_raw_psdu(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/platform/zephyr/drv_radio_zephyr.c");
	const char *submit_func = "static int zb_radio_submit_tx(const u8 *psdu, u8 psdu_len)";
	const char *submit_end = "u8 zb_radio_tx_done_get(void)";
	const char *raw_func = "int zb_platform_radio_send_raw_psdu(const uint8_t *psdu, uint8_t psdu_len)";
	const char *raw_end = "int zb_platform_radio_send_beacon_request(void)";
	const char *beacon_func = "int zb_platform_radio_send_beacon_request(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(strstr(source, "volatile uint32_t zb_radio_tx_trace[8]") != NULL);
	EXPECT_TRUE(contains_between(source, submit_func, submit_end,
				     "zb_radio_tx_trace[3] ="));
	EXPECT_TRUE(contains_between(source, submit_func, submit_end,
				     "zb_radio_tx_trace[4] ="));
	EXPECT_TRUE(contains_between(source, raw_func, raw_end,
				     "zb_radio_tx_trace[0] ="));
	EXPECT_TRUE(contains_between(source, raw_func, raw_end,
				     "zb_radio_tx_trace[1] ="));
	EXPECT_TRUE(contains_between(source, beacon_func,
				     "return zb_radio_submit_tx(beacon_req, ARRAY_SIZE(beacon_req));",
				     "zb_radio_tx_trace[0] ="));
	EXPECT_TRUE(contains_between(source, beacon_func,
				     "return zb_radio_submit_tx(beacon_req, ARRAY_SIZE(beacon_req));",
				     "zb_radio_tx_trace[1] ="));

	free(source);
}

int main(void)
{
	test_platform_tx_trace_is_recorded_for_beaconreq_and_raw_psdu();

	if (failures != 0) {
		fprintf(stderr, "zigbee_platform_tx_trace: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_platform_tx_trace: PASS\n");
	return 0;
}
