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

static void test_apply_tc_context_uses_coord_ext_address_for_centralized_join(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");
	const char *func = "static void nwk_ed_minimal_apply_tc_context(void)";
	const char *centralized_branch = "\tif (centralized) {";
	const char *distributed_branch = "\t} else {";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, centralized_branch,
				     "!ZB_IEEE_ADDR_IS_ZERO(g_zbMacPib.coordExtAddress)"));
	EXPECT_TRUE(contains_between(source, func, centralized_branch,
				     "!ZB_IEEE_ADDR_IS_INVALID(g_zbMacPib.coordExtAddress)"));
	EXPECT_TRUE(contains_between(source, func, centralized_branch,
				     "tcAddr = g_zbMacPib.coordExtAddress;"));
	EXPECT_TRUE(contains_between(source, func, distributed_branch,
				     "ss_securityModeSet(SS_SEMODE_CENTRALIZED);"));

	free(source);
}

int main(void)
{
	test_apply_tc_context_uses_coord_ext_address_for_centralized_join();

	if (failures != 0) {
		fprintf(stderr, "zigbee_tc_context_seed: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_tc_context_seed: PASS\n");
	return 0;
}
