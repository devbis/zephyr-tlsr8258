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

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		fprintf(stderr, "FAIL %s:%d expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_irq_dispatch_uses_shared_lsb_helper_instead_of_manual_bit_scan_loop(void)
{
	char *source = read_file(WORKTREE_ROOT "/arch/tc32/core/irq_manage.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "static ALWAYS_INLINE unsigned int pending_lsb_index(uint32_t pending)"));
	EXPECT_TRUE(contains(source, "return find_lsb_set(pending) - 1u;"));
	EXPECT_FALSE(contains(source, "while ((pending & BIT(irq)) == 0u)"));

	free(source);
}

int main(void)
{
	test_irq_dispatch_uses_shared_lsb_helper_instead_of_manual_bit_scan_loop();

	if (failures != 0) {
		fprintf(stderr, "tc32_irq_pending_dispatch: %d failure(s)\n", failures);
		return 1;
	}

	printf("tc32_irq_pending_dispatch: PASS\n");
	return 0;
}
