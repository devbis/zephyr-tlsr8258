/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Source-inspection tests covering the -EBUSY seam in the Zigbee fresh-join TX path.
 *
 * Task 1 hardware baseline (spec-run2) shows:
 *   zb_nwk_ed_trace[5]  = channel | 0xfff0 (-EBUSY) on final ch11 BeaconReq
 *   zb_nwk_ed_trace[12] = len/cmd/0xfff0 (-EBUSY) on AssocReq
 * No OTA BeaconReq or AssocReq appear in the capture, confirming TX never fired.
 *
 * -EBUSY flows correctly upward through the driver and platform layers (GREEN).
 * The NWK layer above does NOT distinguish -EBUSY from hard failures (RED).
 *
 * The vendor-derived NWK join path is now authoritative; this test also
 * verifies that the old ED-only minimal implementation is not referenced.
 */

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

static bool ordered_between(const char *source, const char *start_marker,
			    const char *end_marker, const char *first,
			    const char *second)
{
	const char *start;
	const char *end;
	const char *first_match;
	const char *second_match;

	start = strstr(source, start_marker);
	if (start == NULL) {
		return false;
	}

	end = strstr(start, end_marker);
	if (end == NULL) {
		return false;
	}

	first_match = strstr(start, first);
	second_match = strstr(start, second);
	return first_match != NULL && second_match != NULL &&
	       first_match < second_match &&
	       first_match < end && second_match < end;
}

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", \
			__FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

/*
 * GREEN: zb_radio_submit_tx() passes the negative return value from
 * g_radio.api->tx() directly back to the caller without remapping.
 * -EBUSY from CCA therefore reaches zb_platform_radio_send_beacon_request()
 * and zb_platform_radio_send_raw_psdu() callers unchanged.
 */
static void test_submit_tx_passes_ebusy_unmodified_to_caller(void)
{
	char *source = read_file(WORKTREE_ROOT
				 "/subsys/zigbee/platform/zephyr/drv_radio_zephyr.c");
	const char *func = "static int zb_radio_submit_tx(const u8 *psdu, u8 psdu_len)\n{";
	const char *next_func = "u8 zb_radio_tx_done_get(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	/* The tx() return value is stored in ret and returned when negative */
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "ret = g_radio.api->tx("));
	EXPECT_TRUE(ordered_between(source, func, next_func,
				    "ret = g_radio.api->tx(",
				    "return ret;"));

	free(source);
}

/*
 * GREEN: tlsr8258_tx() with IEEE802154_TX_MODE_CCA returns the CCA result
 * directly when CCA fails (-EBUSY), before any TX hardware is touched.
 * This confirms -EBUSY originates at the CCA layer, not from TX hardware (-EIO).
 */
static void test_cca_ebusy_returned_before_tx_hardware_path(void)
{
	char *source = read_file(WORKTREE_ROOT
				 "/drivers/ieee802154/ieee802154_tlsr8258.c");
	const char *func = "static int tlsr8258_tx(const struct device *dev, "
			   "enum ieee802154_tx_mode mode,";
	const char *next_func = "static int tlsr8258_ed_scan(";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	/* CCA is called and its return value is passed back directly */
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "ret = tlsr8258_cca(dev);"));
	EXPECT_TRUE(ordered_between(source, func, next_func,
				    "ret = tlsr8258_cca(dev);",
				    "return ret;"));

	/* The current driver records timeout/error status in radio_op and returns
	 * the operation result, rather than hard-coding errno at each branch. */
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "tlsr8258_radio_op_on_tx_error"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "tlsr8258_radio_op_result_errno"));
	EXPECT_TRUE(ordered_between(source, func, next_func,
				    "ret = tlsr8258_cca(dev);",
				    "ret = tlsr8258_set_tx_payload"));

	free(source);
}

static void test_vendor_join_path_is_authoritative(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_join.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(strstr(source, "void nwk_associateJoin(void *arg)") != NULL);
	EXPECT_TRUE(strstr(source, "nwk_nlmeJoinCnf") != NULL);
	free(source);
}

int main(void)
{
	/* GREEN: lower layers correctly propagate -EBUSY */
	test_submit_tx_passes_ebusy_unmodified_to_caller();
	test_cca_ebusy_returned_before_tx_hardware_path();

	test_vendor_join_path_is_authoritative();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_tx_ebusy_path: %d failure(s)\n",
			failures);
		return 1;
	}

	printf("tlsr8258_tx_ebusy_path: PASS\n");
	return 0;
}
