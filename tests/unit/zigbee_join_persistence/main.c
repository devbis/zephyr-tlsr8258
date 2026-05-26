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

static bool function_contains_in_order(const char *source, const char *func_name,
				       const char *needle_a, const char *needle_b,
				       const char *next_func_name)
{
	const char *func;
	const char *next;
	const char *a;
	const char *b;

	func = strstr(source, func_name);
	if (func == NULL) {
		return false;
	}

	next = strstr(func, next_func_name);
	if (next == NULL) {
		return false;
	}

	a = strstr(func, needle_a);
	b = strstr(func, needle_b);
	return a != NULL && b != NULL && a < b && a < next && b < next;
}

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_fresh_association_join_persists_state_before_success_confirm(void)
{
	char *source = read_file(REPO_ROOT "/libzigbee/src/nwk_join.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(function_contains_in_order(
		source,
		"void tl_zbMacMlmeAssociateConfirmHandler(void *arg)",
		"zb_info_save(NULL);",
		"nwk_nlmeJoinCnf(arg, 0, NWK_STATUS_SUCCESS, 0);",
		"void nwk_rejoinScanCnfHandler(void *arg)"));

	free(source);
}

static void test_rejoin_path_still_persists_state(void)
{
	char *source = read_file(REPO_ROOT "/libzigbee/src/nwk_join.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(function_contains_in_order(
		source,
		"void tl_zbNwkRejoinRespCmdHandler(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd)",
		"zb_info_save(NULL);",
		"zdo_device_announce_send();",
		"void nwk_directJoinScanCnfHandler(void *arg)"));

	free(source);
}

int main(void)
{
	test_fresh_association_join_persists_state_before_success_confirm();
	test_rejoin_path_still_persists_state();

	if (failures != 0) {
		fprintf(stderr, "zigbee_join_persistence: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_join_persistence: PASS\n");
	return 0;
}
