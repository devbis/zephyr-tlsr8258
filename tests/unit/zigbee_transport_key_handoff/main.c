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

static bool function_contains(const char *source, const char *func_marker,
			      const char *needle)
{
	const char *func = strstr(source, func_marker);

	if (func == NULL) {
		return false;
	}

	return strstr(func, needle) != NULL;
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

static void test_transport_key_done_schedules_join_completion_on_task_queue(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/nwk/nwk_ed_minimal.c");
	const char *func = "void tl_zbNwkEdMinimalTransportKeyDone(void)";
	const char *next_func = "static void nwk_ed_minimal_post_join_announce_task(void *arg)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "TL_SCHEDULE_TASK(nwk_ed_minimal_transport_key_done_task, NULL)"));
	EXPECT_FALSE(contains_between(source, func, next_func,
				      "nwk_ed_minimal_complete_join(g_nwkEdCtx.interviewRejoinMode);"));
	EXPECT_TRUE(function_contains(source,
				      "static void nwk_ed_minimal_transport_key_done_task(void *arg)",
				      "nwk_ed_minimal_complete_join(g_nwkEdCtx.interviewRejoinMode);"));

	free(source);
}

static void test_aps_transport_key_decrypt_hashes_key_load_key_like_vendor(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/mac/mac_trx_compat.c");
	const char *func = "static bool zb_minimal_decrypt_aps_payload(u8 *aps_psdu, u8 aps_len, zb_minimal_aps_frame_t *frame)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(function_contains(source, func,
				      "u8 pad = (frame->key_id == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;"));
	EXPECT_TRUE(function_contains(source, func,
				      "if (ss_keyHash(&pad, key, hashed_key) == RET_OK)"));
	EXPECT_FALSE(function_contains(source, func,
				       "memcmp(key, tcLinkKeyCentralDefault, SEC_KEY_LEN) == 0"));
	EXPECT_FALSE(function_contains(source, func, "u8 hash_input = 0U;"));

	free(source);
}

int main(void)
{
	test_transport_key_done_schedules_join_completion_on_task_queue();
	test_aps_transport_key_decrypt_hashes_key_load_key_like_vendor();

	if (failures != 0) {
		fprintf(stderr, "zigbee_transport_key_handoff: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_transport_key_handoff: PASS\n");
	return 0;
}
