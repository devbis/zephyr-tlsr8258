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

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		fprintf(stderr, "FAIL %s:%d expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_bootstrap_ready_gates_restored_poll_on_zdo_idle(void)
{
	char *source = read_file(WORKTREE_ROOT "/samples/zigbee/zigbee_shell/src/app_bdb.c");
	const char *func = "void app_bdb_bootstrap_ready(void)";
	const char *next_func = "bool app_bdb_should_start_commissioning(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "zdo_ifZdoNwkManagerIdle()"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "bool idle = zdo_ifZdoNwkManagerIdle();"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "app_bdb_activate_poll_rate();"));
	EXPECT_FALSE(contains_between(source, func, next_func,
				      "if (zb_isDeviceJoinedNwk()) {\n\t\t\t\tapp_bdb_activate_poll_rate();"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "if (joined && idle) {"));

	free(source);
}

int main(void)
{
	test_bootstrap_ready_gates_restored_poll_on_zdo_idle();

	if (failures != 0) {
		fprintf(stderr, "zigbee_restore_poll_gate: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_restore_poll_gate: PASS\n");
	return 0;
}
