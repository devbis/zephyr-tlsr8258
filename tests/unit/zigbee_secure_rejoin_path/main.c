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

static void test_rejoin_start_uses_dedicated_nwk_rejoin_request(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");
	const char *func = "bool tl_zbNwkEdMinimalRejoinStart(u32 scanChannels, u8 scanDuration, bool withBackoff)";
	const char *next_func = "void tl_zbNwkEdMinimalOperationAbort(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "nwk_ed_minimal_send_rejoin_request("));
	EXPECT_FALSE(contains_between(source, func, next_func,
				      "nwk_ed_minimal_start_assoc(TRUE)"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_nwkEdCtx.activeChannel = g_nwkEdCtx.fixedJoinChannel;"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_nwkEdCtx.activePanId = g_nwkEdCtx.fixedJoinPanId;"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_nwkEdCtx.activeParentShortAddr = g_nwkEdCtx.fixedJoinShortAddr;"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_nwkEdCtx.activeShortAddr = g_zbMacPib.shortAddress;"));
	EXPECT_TRUE(contains_between(source,
				     "static bool nwk_ed_minimal_send_rejoin_request(void)",
				     "static bool nwk_ed_minimal_start_assoc(bool rejoinMode)",
				     "NWK_CMD_REJOIN_REQUEST"));

	free(source);
}

static void test_joined_rx_routes_rejoin_response_into_minimal_rejoin_handler(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/mac/mac_trx_compat.c");
	const char *func = "static void zb_minimal_handle_joined_data_frame(u8 *psdu, u8 len)";
	const char *next_func = "void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source,
				     "__attribute__((weak)) void tl_zbNwkEdMinimalRejoinResponseReceived(",
				     "__attribute__((weak)) void tl_zbMinimalZdoResponseIndication(",
				     "ARG_UNUSED(status);"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "NWK_CMD_REJOIN_RESPONSE"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "tl_zbNwkEdMinimalRejoinResponseReceived("));

	free(source);
}

static void test_rejoin_request_clears_wire_security_level_after_ccm_auth(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");
	const char *func = "static bool nwk_ed_minimal_send_rejoin_request(void)\n{";
	const char *next_func = "static bool nwk_ed_minimal_start_assoc(bool rejoinMode)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "frame[sec_ctrl_idx] = NWK_ED_MINIMAL_NWK_SEC_CTRL_WIRE;"));
	EXPECT_TRUE(ordered_between(source, func, next_func,
				    "enc_len = zb_minimal_ccm_encrypt_auth(",
				    "frame[sec_ctrl_idx] = NWK_ED_MINIMAL_NWK_SEC_CTRL_WIRE;"));

	free(source);
}

int main(void)
{
	test_rejoin_start_uses_dedicated_nwk_rejoin_request();
	test_joined_rx_routes_rejoin_response_into_minimal_rejoin_handler();
	test_rejoin_request_clears_wire_security_level_after_ccm_auth();

	if (failures != 0) {
		fprintf(stderr, "zigbee_secure_rejoin_path: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_secure_rejoin_path: PASS\n");
	return 0;
}
