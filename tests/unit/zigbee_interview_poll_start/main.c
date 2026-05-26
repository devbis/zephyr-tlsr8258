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

int main(void)
{
	test_interview_poll_start_can_restart_active_interview();
	test_interview_poll_start_can_kick_pre_interview_assoc_window();
	test_joined_rx_gate_allows_pre_interview_assoc_window();
	test_joined_tx_gate_allows_pre_interview_assoc_window();

	if (failures != 0) {
		fprintf(stderr, "zigbee_interview_poll_start: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_interview_poll_start: PASS\n");
	return 0;
}
