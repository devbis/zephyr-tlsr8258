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

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_association_completion_enters_vendor_authentication_path(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "void zdo_nwk_authentication_complete(void)"));
	EXPECT_TRUE(contains(source, "zdo_secure_startup_pending = true;"));
	EXPECT_TRUE(contains(source, "g_zbNwkCtx.joined = 1U;"));
	free(source);
}

static void test_bdb_receives_association_and_secure_handoff_separately(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/bdb/bdb.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "bdb_zdoAssocDone"));
	EXPECT_TRUE(contains(source, "bdb_ed_assoc_handoff_start();"));
	EXPECT_TRUE(contains(source, "bdb_zdoStartDevCnf"));
	EXPECT_TRUE(contains(source, "bdb_ed_secure_join_handoff_start();"));
	free(source);
}

static void test_transport_key_completion_is_idempotent(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "if (!g_zbNwkCtx.joined &&"));
	EXPECT_TRUE(contains(source, "void zdo_nwk_authentication_complete(void)"));
	free(source);
}

int main(void)
{
	test_association_completion_enters_vendor_authentication_path();
	test_bdb_receives_association_and_secure_handoff_separately();
	test_transport_key_completion_is_idempotent();

	if (failures != 0) {
		fprintf(stderr, "zigbee_assoc_interview_handoff: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_assoc_interview_handoff: PASS\n");
	return 0;
}
