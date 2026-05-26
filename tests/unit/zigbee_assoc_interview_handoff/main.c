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

static void test_assoc_success_notifies_bdb_before_entering_interview(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");
	const char *func = "static void nwk_ed_minimal_handle_assoc_rsp_event(const nwk_ed_minimal_rx_evt_t *evt)";
	const char *unauth_branch = "\t\tif (!aps_ib.aps_authenticated) {";
	const char *branch_end = "\t\tnwk_ed_minimal_complete_join(rejoinMode);";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, branch_end,
				     "tl_zdoEdMinimalJoinDone(ZDO_SUCCESS, rejoinMode);"));
	EXPECT_TRUE(contains_between(source, unauth_branch, branch_end,
				     "nwk_ed_minimal_enter_interview(rejoinMode);"));
	EXPECT_TRUE(contains_between(source, unauth_branch, branch_end,
				     "tl_zdoEdMinimalJoinDone(ZDO_SUCCESS, rejoinMode);"));

	free(source);
}

int main(void)
{
	test_assoc_success_notifies_bdb_before_entering_interview();

	if (failures != 0) {
		fprintf(stderr, "zigbee_assoc_interview_handoff: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_assoc_interview_handoff: PASS\n");
	return 0;
}
