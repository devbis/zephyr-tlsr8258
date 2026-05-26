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

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_joined_rx_routes_nwk_leave_into_minimal_leave_flow(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/mac/mac_trx_compat.c");
	const char *command_branch = "\tif (nwk.frame_type == FRAME_TYPE_COMMAND) {";
	const char *command_end = "\tif (nwk.frame_type != FRAME_TYPE_DATA) {";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "static bool zb_minimal_handle_nwk_leave_command(const zb_minimal_nwk_frame_t *nwk)"));
	EXPECT_TRUE(contains_between(source, command_branch, command_end,
				     "if (zb_minimal_handle_nwk_leave_command(&nwk)) {"));
	EXPECT_TRUE(contains(source, "nwk->payload[0] != NWK_CMD_LEAVE"));
	EXPECT_TRUE(contains(source, "leave_options = nwk->payload[1];"));
	EXPECT_TRUE(contains(source, "send_leave_command = (leave_options & ZB_MINIMAL_NWK_LEAVE_REQUEST) != 0U;"));
	EXPECT_TRUE(contains(source, "if (leave.send_leave_command) {"));
	EXPECT_TRUE(contains(source, "if (leave.send_zdo_response) {"));

	free(source);
}

static void test_parent_leave_with_rejoin_preserves_rejoin_semantics(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/mac/mac_trx_compat.c");
	const char *leave_task = "static void zb_minimal_leave_task(void *arg)";
	const char *queue_leave = "static bool zb_minimal_queue_leave(u16 src_nwk_addr, u8 zdo_seq, u8 leave_options,";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "zb_minimal_queue_leave(nwk->src_addr, 0U, leave_options, send_leave_command, false)"));
	EXPECT_TRUE(contains_between(source, leave_task, queue_leave,
				     "if ((leave.leave_options & ZB_MINIMAL_NWK_LEAVE_REJOIN) != 0U) {"));
	EXPECT_TRUE(contains_between(source, leave_task, queue_leave,
				     "zdo_ed_minimal_rejoin_restart_prepare();"));
	EXPECT_TRUE(contains_between(source, leave_task, queue_leave,
				     "zdo_nwkRejoinStart((u32)1U << g_zbMacPib.phyChannelCur,"));

	free(source);
}

static void test_duplicate_parent_leave_rejoin_is_ignored_while_rejoin_pending(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/mac/mac_trx_compat.c");
	const char *func = "static bool zb_minimal_handle_nwk_leave_command(const zb_minimal_nwk_frame_t *nwk)";
	const char *next_func = "static bool zb_minimal_handle_zdo_request(u16 src_nwk_addr, const zb_minimal_aps_frame_t *aps)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "if (((leave_options & ZB_MINIMAL_NWK_LEAVE_REJOIN) != 0U) && zb_isUnderRejoinMode()) {"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "return true;"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "if (!zb_minimal_queue_leave(nwk->src_addr, 0U, leave_options, send_leave_command, false)) {"));

	free(source);
}

static void test_rejoin_restart_prepare_clears_minimal_rejoin_state(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_ed_minimal.c");
	const char *func = "void zdo_ed_minimal_rejoin_restart_prepare(void)";
	const char *next_func = "zdo_status_t zdo_nwkRejoinStart(u32 scanChannels, u8 scanDuration)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_zdoEdAsync.rejoinPending = FALSE;"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_zdoEdAsync.rejoinWithBackoff = FALSE;"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "g_zdoUnderRejoinMode = FALSE;"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "tl_zbNwkEdMinimalOperationAbort();"));

	free(source);
}

int main(void)
{
	test_joined_rx_routes_nwk_leave_into_minimal_leave_flow();
	test_parent_leave_with_rejoin_preserves_rejoin_semantics();
	test_duplicate_parent_leave_rejoin_is_ignored_while_rejoin_pending();
	test_rejoin_restart_prepare_clears_minimal_rejoin_state();

	if (failures != 0) {
		fprintf(stderr, "zigbee_nwk_leave_handling: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_nwk_leave_handling: PASS\n");
	return 0;
}
