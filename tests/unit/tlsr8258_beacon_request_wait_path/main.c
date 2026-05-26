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

static bool ordered_between(const char *source, const char *start_marker,
			    const char *end_marker, const char *first,
			    const char *second)
{
	const char *start;
	const char *end;
	const char *first_match;
	const char *second_match;

	start = strstr(source, start_marker);
	if (start == NULL) {
		return false;
	}

	end = strstr(start, end_marker);
	if (end == NULL) {
		return false;
	}

	first_match = strstr(start, first);
	second_match = strstr(start, second);
	return first_match != NULL && second_match != NULL &&
	       first_match < second_match &&
	       first_match < end && second_match < end;
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

static void test_beacon_request_keeps_post_tx_rx_window_open_without_requiring_ack(void)
{
	char *source = read_file(WORKTREE_ROOT "/drivers/ieee802154/ieee802154_tlsr8258.c");
	const char *func = "static bool tlsr8258_wait_for_post_poll_rx(const uint8_t *tx_psdu, uint8_t tx_psdu_len)\n{";
	const char *next_func = "static void tlsr8258_rf_off(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains_between(source, func, next_func,
				     "beacon_req = tlsr8258_psdu_is_beacon_request(tx_psdu, tx_psdu_len);"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "if (!data_req && !beacon_req && ((tx_psdu[0] & TLSR8258_ACK_REQUEST) == 0u)) {"));
	EXPECT_TRUE(ordered_between(source, func, next_func,
				    "beacon_req = tlsr8258_psdu_is_beacon_request(tx_psdu, tx_psdu_len);",
				    "if (!data_req && !beacon_req && ((tx_psdu[0] & TLSR8258_ACK_REQUEST) == 0u)) {"));
	EXPECT_TRUE(contains_between(source, func, next_func,
				     "return require_ack ? ack_seen : true;"));
	EXPECT_FALSE(contains_between(source, func, next_func,
				      "if (!data_req && ((tx_psdu[0] & TLSR8258_ACK_REQUEST) == 0u)) {"));

	free(source);
}

int main(void)
{
	test_beacon_request_keeps_post_tx_rx_window_open_without_requiring_ack();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_beacon_request_wait_path: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_beacon_request_wait_path: PASS\n");
	return 0;
}
