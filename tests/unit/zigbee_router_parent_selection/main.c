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

static void test_router_parent_selection_uses_router_capacity(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_discovery.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source,
			     "#if (defined(ZB_ROUTER_ROLE) && ZB_ROUTER_ROLE)\n"
			     "\t\tif (entry.stackProfile == g_zbNIB.stackProfile && entry.routerCapacity) {\n"
			     "\t\t\tentry.potentialParent = 1;\n"
			     "\t\t}\n"
			     "#else\n"
			     "\t\tif (entry.stackProfile == g_zbNIB.stackProfile && entry.edCapacity) {\n"
			     "\t\t\tentry.potentialParent = 1;\n"
			     "\t\t}\n"
			     "#endif"));

	free(source);
}

int main(void)
{
	test_router_parent_selection_uses_router_capacity();

	if (failures != 0) {
		fprintf(stderr, "zigbee_router_parent_selection: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_router_parent_selection: PASS\n");
	return 0;
}
