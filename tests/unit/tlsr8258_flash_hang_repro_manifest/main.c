/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
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

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void expect_contains(const char *path, const char *needle)
{
	char *source = read_file(path);

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(strstr(source, needle) != NULL);
	free(source);
}

static void test_tlsr8258_flash_hang_repro_sample_exists(void)
{
	expect_contains(WORKTREE_ROOT "/samples/boards/telink/tlsr8258_flash_hang_repro/sample.yaml",
			"TLSR8258 flash hang repro");
	expect_contains(WORKTREE_ROOT "/samples/boards/telink/tlsr8258_flash_hang_repro/prj.conf",
			"CONFIG_TLSR8258_FLASH_HANG_REPRO_MODE_DIRECT=y");
	expect_contains(WORKTREE_ROOT "/samples/boards/telink/tlsr8258_flash_hang_repro/src/main.c",
			"tlsr8258_flash_hang_repro");
	expect_contains(WORKTREE_ROOT "/samples/boards/telink/tlsr8258_flash_hang_repro/Kconfig",
			"config TLSR8258_FLASH_HANG_REPRO_MODE_NVS_IDLE_LOOP");
}

int main(void)
{
	test_tlsr8258_flash_hang_repro_sample_exists();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_flash_hang_repro_manifest: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_flash_hang_repro_manifest: PASS\n");
	return 0;
}
