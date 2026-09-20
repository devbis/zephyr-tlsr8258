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

/*
 * zdo_nlme_status_indication() (subsys/zigbee/zdo/zdo_nwk_manager.c) used to
 * only protect ROUTER builds from abandoning a live network context after a
 * short burst of parent-poll NO_ACKs; an ED hitting the same retry-threshold
 * would unconditionally call zdo_nwkDirectJoinStart(), wiping g_zbNwkCtx.joined
 * and restarting full discovery even though the parent link was still fine.
 * Guard against regressing that fix.
 */
static void test_status_indication_guards_both_roles_via_live_join_context(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, "void zdo_nlme_status_indication(void *arg)",
				     "zdo_status_t zdo_nwkDiscoveryStart",
				     "if (zdo_live_join_context()) {"));

	/* The zdo_live_join_context() guard above must NOT be gated behind
	 * ZB_ROUTER_ROLE -- that was exactly the bug (ED had no equivalent
	 * protection). */
	EXPECT_TRUE(!contains_between(source, "void zdo_nlme_status_indication(void *arg)",
				      "if (zdo_live_join_context()) {",
				      "#if defined(ZB_ROUTER_ROLE)"));

	free(source);
}

/*
 * Once the retry budget is genuinely exhausted (zdo_live_join_context()
 * returned false), the ED path should prefer the cheaper zdo_nwkRejoinStart()
 * (reuses the existing NWK key / short address) over
 * zdo_nwkDirectJoinStart() (full re-association from scratch), mirroring the
 * sibling nwkEndDevTimeoutRejoin() ED recovery path.
 */
static void test_ed_fallback_prefers_rejoin_over_direct_join(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, "void zdo_nlme_status_indication(void *arg)",
				     "zdo_status_t zdo_nwkDiscoveryStart",
				     "#else"));
	EXPECT_TRUE(contains_between(source, "void zdo_nlme_status_indication(void *arg)",
				     "zdo_status_t zdo_nwkDiscoveryStart",
				     "zdo_nwkRejoinStart(1UL << g_zbInfo.macPib.phyChannelCur,"));

	free(source);
}

int main(void)
{
	test_status_indication_guards_both_roles_via_live_join_context();
	test_ed_fallback_prefers_rejoin_over_direct_join();

	if (failures != 0) {
		fprintf(stderr, "zigbee_ed_parent_link_recovery: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_ed_parent_link_recovery: PASS\n");
	return 0;
}
