/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Regression test: late-success AssocResp / associationReqOrigBuffer race.
 *
 * Timeline that causes the bug:
 *   1. ASSOC_REQ sent; associationReqOrigBuffer set.
 *   2. AssocResp received on-air; fast-handoff sets macPib.shortAddress.
 *   3. Deferred MAC task for AssocResp posted to event queue.
 *   4. Wait-timer fires before the task runs: posts NO_DATA CNF and
 *      clears associationReqOrigBuffer.
 *   5. Deferred task runs: tl_zbMlmeCmdAssociateRespRecvd() sees
 *      associationReqOrigBuffer == NULL and bails — discarding the
 *      coordinator's real SUCCESS response.
 *
 * Desired behaviour after the fix:
 *   When associationReqOrigBuffer is NULL but the coordinator's status
 *   byte (payload[3]) is MAC_SUCCESS, the handler must NOT bail.  It
 *   must fall through to the success path and post an
 *   ASSOCIATE_CNF(SUCCESS) to the NWK layer.  Only non-SUCCESS responses
 *   should be dropped in the NULL-origbuf branch.
 *
 * Fix site: subsys/zigbee/mac/mac_mlme.c,
 *           tl_zbMlmeCmdAssociateRespRecvd().
 */

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

/*
 * Returns true when needle appears in the region of source that
 * starts at the first occurrence of start_marker and ends just before
 * the first occurrence of end_marker found after start_marker.
 */
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
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", \
			__FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		fprintf(stderr, "FAIL %s:%d expected false: %s\n", \
			__FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

/*
 * Before the fix: associationReqOrigBuffer == NULL causes an
 * unconditional bail (zb_buf_free + return) with no status check.
 *
 * After the fix: the null-origbuf branch must first test payload[3]
 * against MAC_SUCCESS.  Only non-SUCCESS responses bail; MAC_SUCCESS
 * falls through to post the CNF.
 *
 * We verify this by checking that "payload[3] != MAC_SUCCESS" appears
 * inside the null-origbuf branch, specifically between the null check
 * and the first call to tl_zbMacAssociateRespReceived() (which the fix
 * places at the end of the late-success path inside that branch).
 *
 * RED: before the fix, the region between the null check and
 * tl_zbMacAssociateRespReceived() contains only
 * "zb_buf_free(buf); return;" — no status check — so this assertion
 * fails.
 */
static void test_null_origbuf_branch_checks_status_before_bail(void)
{
	char *source = read_file(
		WORKTREE_ROOT "/subsys/zigbee/mac/mac_mlme.c");
	/*
	 * The null-origbuf check and the first call to
	 * tl_zbMacAssociateRespReceived() bracket the region we inspect.
	 * After the fix the call is placed at the end of the late-success
	 * path inside the null-origbuf branch.
	 */
	const char *null_check = "if (associationReqOrigBuffer == NULL) {";
	const char *resp_received = "tl_zbMacAssociateRespReceived()";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, null_check, resp_received,
				     "payload[3] != MAC_SUCCESS"));

	free(source);
}

/*
 * Companion assertion: the bail (zb_buf_free(buf)) must sit inside a
 * "if (payload[3] != MAC_SUCCESS)" guard, not run unconditionally.
 *
 * RED: before the fix no such guard exists, so the start_marker is
 * not found and the check returns false.
 */
static void test_null_origbuf_bail_is_guarded_by_non_success_check(void)
{
	char *source = read_file(
		WORKTREE_ROOT "/subsys/zigbee/mac/mac_mlme.c");
	const char *non_success_guard = "if (payload[3] != MAC_SUCCESS) {";
	/*
	 * tl_zbMacAssociateRespReceived() is the first landmark after the
	 * guard block in the fixed code.
	 */
	const char *resp_received = "tl_zbMacAssociateRespReceived()";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, non_success_guard,
				     resp_received, "zb_buf_free(buf)"));

	free(source);
}

int main(void)
{
	test_null_origbuf_branch_checks_status_before_bail();
	test_null_origbuf_bail_is_guarded_by_non_success_check();

	if (failures != 0) {
		fprintf(stderr,
			"zigbee_assoc_late_success_race: %d failure(s)\n",
			failures);
		return 1;
	}

	printf("zigbee_assoc_late_success_race: PASS\n");
	return 0;
}
