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

#define EXPECT_CONTAINS(haystack, needle) EXPECT_TRUE(strstr((haystack), (needle)) != NULL)
#define EXPECT_NOT_CONTAINS(haystack, needle) EXPECT_TRUE(strstr((haystack), (needle)) == NULL)

static void test_tc32_linker_uses_explicit_boot_mirror_section(void)
{
	char *source =
		read_file(WORKTREE_ROOT "/include/zephyr/arch/tc32/linker.ld");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_CONTAINS(source, "SECTION_PROLOGUE(.boot_ram_mirror,,)");
	EXPECT_CONTAINS(source, "PROVIDE(__boot_ram_mirror_start = .);");
	EXPECT_CONTAINS(source, "*(.boot_ram_mirror)");
	EXPECT_NOT_CONTAINS(source, "SECTION_PROLOGUE(.ram_code,,)");

	free(source);
}

static void test_runtime_flash_path_uses_ramfunc_not_boot_mirror(void)
{
	char *source = read_file(WORKTREE_ROOT "/drivers/flash/flash_tlsr8258.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_CONTAINS(source, "__ramfunc int tlsr8258_flash_read_ram(");
	EXPECT_CONTAINS(source, "__ramfunc int tlsr8258_flash_write_page_ram(");
	EXPECT_CONTAINS(source, "__ramfunc int tlsr8258_flash_erase_sector_ram(");
	EXPECT_NOT_CONTAINS(source, "section(\".ram_code\")");
	EXPECT_NOT_CONTAINS(source, "TLSR8258_RAM_CODE");

	free(source);
}

static void test_boot_mirror_macro_is_used_for_startup_paths(void)
{
	char *arch_h = read_file(WORKTREE_ROOT "/include/zephyr/arch/tc32/arch.h");
	char *irq = read_file(WORKTREE_ROOT "/arch/tc32/core/irq_manage.c");
	char *pm = read_file(WORKTREE_ROOT "/soc/telink/tlsr/tlsr825x/power.c");

	EXPECT_TRUE(arch_h != NULL);
	EXPECT_TRUE(irq != NULL);
	EXPECT_TRUE(pm != NULL);
	if (arch_h == NULL || irq == NULL || pm == NULL) {
		free(arch_h);
		free(irq);
		free(pm);
		return;
	}

	EXPECT_CONTAINS(arch_h, "TC32_BOOT_RAM_MIRROR_CODE");
	EXPECT_CONTAINS(arch_h, "section(\".boot_ram_mirror\")");
	EXPECT_CONTAINS(irq, "void TC32_BOOT_RAM_MIRROR_CODE z_tc32_handle_irqs(void)");
	EXPECT_CONTAINS(pm, "static void TC32_BOOT_RAM_MIRROR_CODE tlsr8258_pm_sleep_start(void)");

	free(arch_h);
	free(irq);
	free(pm);
}

int main(void)
{
	test_tc32_linker_uses_explicit_boot_mirror_section();
	test_runtime_flash_path_uses_ramfunc_not_boot_mirror();
	test_boot_mirror_macro_is_used_for_startup_paths();

	if (failures != 0) {
		fprintf(stderr, "tc32_boot_mirror_contract: %d failure(s)\n", failures);
		return 1;
	}

	printf("tc32_boot_mirror_contract: PASS\n");
	return 0;
}
