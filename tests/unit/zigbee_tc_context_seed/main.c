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

static void test_fixed_join_seeds_trust_center_context(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_libzigbee.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "void zb_ed_fixed_join_target("));
	EXPECT_TRUE(contains(source, "memcpy(g_zbInfo.macPib.coordExtAddress, tc_addr, EXT_ADDR_LEN);"));
	EXPECT_TRUE(contains(source, "memcpy(ss_ib.trust_center_address, tc_addr, EXT_ADDR_LEN);"));
	EXPECT_TRUE(contains(source, "zb_preConfigNwkKey((u8 *)nwk_key, FALSE);"));
	free(source);
}

static void test_security_service_consumes_seeded_trust_center_address(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/ss/ss_apsEnDecrypt.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "ss_ib.trust_center_address"));
	EXPECT_TRUE(contains(source, "SS_SECUR_KEY_TRANSPORT_KEY"));
	free(source);
}

int main(void)
{
	test_fixed_join_seeds_trust_center_context();
	test_security_service_consumes_seeded_trust_center_address();

	if (failures != 0) {
		fprintf(stderr, "zigbee_tc_context_seed: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_tc_context_seed: PASS\n");
	return 0;
}
