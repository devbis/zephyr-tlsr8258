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

static void test_interview_poll_start_can_restart_active_interview(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source,
			     "if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW ||\n"
			     "\t    g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_JOINING ||\n"
			     "\t    g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN) {\n"
			     "\t\tnwk_ed_minimal_timer_start(1U);\n"
			     "\t\treturn;\n"
			     "\t}"));
	EXPECT_TRUE(contains(source,
			     "if (!g_zbNwkCtx.joined || g_nwkEdCtx.activePanId == MAC_INVALID_PANID ||\n"
			     "\t    g_nwkEdCtx.activeParentShortAddr == MAC_SHORT_ADDR_NONE) {"));

	free(source);
}

static void test_interview_poll_start_can_kick_pre_interview_assoc_window(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source,
			     "if (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW ||\n"
			     "\t    g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_JOINING ||\n"
			     "\t    g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN) {\n"
			     "\t\tnwk_ed_minimal_timer_start(1U);\n"
			     "\t\treturn;\n"
			     "\t}"));

	free(source);
}

static void test_joined_rx_gate_allows_pre_interview_assoc_window(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source,
			     "bool tl_zbNwkEdMinimalCanProcessDataFrames(void)\n"
			     "{\n"
			     "\treturn g_zbNwkCtx.joined ||\n"
			     "\t       (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_INTERVIEW) ||\n"
			     "\t       (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_JOINING) ||\n"
			     "\t       (g_nwkEdCtx.state == NWK_ED_MINIMAL_STATE_REJOIN);\n"
			     "}"));

	free(source);
}

static void test_joined_tx_gate_allows_pre_interview_assoc_window(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api_zdo_send_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source,
			     "if (!zb_isDeviceJoinedNwk() && !tl_zbNwkEdMinimalCanProcessDataFrames()) {\n"
			     "\t\treturn APS_STATUS_ILLEGAL_REQUEST;\n"
			     "\t}"));

	free(source);
}

static void test_post_join_announce_arms_interview_poll_unconditionally(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");
	const char *func = "static void nwk_ed_minimal_post_join_announce_task(void *arg)";
	const char *cond = "if (zb_zdoSendDevAnnance() == ZDO_SUCCESS)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	/* InterviewPollStart must be armed regardless of whether dev-announce
	 * delivery succeeds, so the coordinator's ZDO queries are received
	 * even if the announce frame is lost.  The call must therefore appear
	 * before — not inside — the if (zb_zdoSendDevAnnance() == ZDO_SUCCESS)
	 * conditional in nwk_ed_minimal_post_join_announce_task. */
	EXPECT_TRUE(contains_between(source, func, cond,
				     "tl_zbNwkEdMinimalInterviewPollStart(0U, 0U)"));

	free(source);
}

static void test_assoc_handoff_arms_bdb_interview_poll(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/bdb/bdb.c");
	const char *func = "static void bdb_ed_assoc_handoff_start(void)";
	const char *cond = "g_bdbCtx.retrieveTcLkKeyTimer = TL_ZB_TIMER_SCHEDULE";
	const char *next_func = "static void bdb_ed_secure_join_handoff_start(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, cond, "bdb_ed_post_join_poll_kick();"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "TL_ZB_TIMER_SCHEDULE(bdb_ed_assoc_request_key_guard, NULL, 200);"));

	free(source);
}

static void test_locked_fixed_target_is_not_replaced_by_traffic_hints(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source,
			     "static bool nwk_ed_minimal_fixed_join_target_locked(void)\n"
			     "{\n"
			     "\treturn g_nwkEdCtx.fixedJoinValid &&\n"
			     "\t       (g_nwkEdCtx.fixedJoinUsesPreconfiguredNwkKey ||\n"
			     "\t\t(!ZB_IEEE_ADDR_IS_ZERO(g_nwkEdCtx.fixedJoinTcAddr) &&\n"
			     "\t\t !ZB_IEEE_ADDR_IS_INVALID(g_nwkEdCtx.fixedJoinTcAddr)));\n"
			     "}"));
	EXPECT_TRUE(contains(source,
			     "if (nwk_ed_minimal_fixed_join_target_locked()) {\n"
			     "\t\tparentShortAddr = g_nwkEdCtx.fixedJoinShortAddr;\n"
			     "\t\tpanId = g_nwkEdCtx.fixedJoinPanId;\n"
			     "\t\tZB_EXTPANID_COPY(extPanId, g_nwkEdCtx.fixedJoinExtPanId);\n"
			     "\t\tchannel = g_nwkEdCtx.fixedJoinChannel;\n"
			     "\t} else if (g_nwkEdCtx.parentCandidateValid) {"));
	EXPECT_TRUE(contains_between(source,
				     "static void nwk_ed_minimal_handle_traffic_candidate_event(const nwk_ed_minimal_rx_evt_t *evt)",
				     "LOG_INF(\"traffic candidate: pan 0x%04x parent 0x%04x ch %u rssi %d\",",
				     "if (!nwk_ed_minimal_fixed_join_target_locked()) {\n"
				     "\t\t\ttl_zbNwkEdMinimalSetFixedJoinTarget("));

	free(source);
}

int main(void)
{
	test_interview_poll_start_can_restart_active_interview();
	test_interview_poll_start_can_kick_pre_interview_assoc_window();
	test_joined_rx_gate_allows_pre_interview_assoc_window();
	test_joined_tx_gate_allows_pre_interview_assoc_window();
	test_post_join_announce_arms_interview_poll_unconditionally();
	test_assoc_handoff_arms_bdb_interview_poll();
	test_locked_fixed_target_is_not_replaced_by_traffic_hints();

	if (failures != 0) {
		fprintf(stderr, "zigbee_interview_poll_start: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_interview_poll_start: PASS\n");
	return 0;
}
