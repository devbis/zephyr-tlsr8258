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
	return source != NULL && strstr(source, needle) != NULL;
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

static void test_mac_internal_exposes_typed_timer_state(void)
{
	char *header = read_file(WORKTREE_ROOT "/subsys/zigbee/mac/includes/mac_internal.h");

	EXPECT_TRUE(header != NULL);
	if (header == NULL) {
		return;
	}

	EXPECT_TRUE(contains(header, "typedef struct {"));
	EXPECT_TRUE(contains(header, "int (*cb)(void *arg);"));
	EXPECT_TRUE(contains(header, "u32 deadline;"));
	EXPECT_TRUE(contains(header, "u8 state;"));
	EXPECT_TRUE(contains(header, "} mac_timer_evt_t;"));
	EXPECT_TRUE(contains(header, "extern mac_timer_evt_t g_macTimerEvt;"));
	EXPECT_FALSE(contains(header, "MAC_TIMER_EVT_CB_OFFSET"));
	EXPECT_FALSE(contains(header, "MAC_TIMER_EVT_DEADLINE_OFFSET"));
	EXPECT_FALSE(contains(header, "MAC_TIMER_EVT_STATE_OFFSET"));
	EXPECT_FALSE(contains(header, "MAC_TIMER_EVT_STORAGE_SIZE"));
	EXPECT_FALSE(contains(header, "extern u8 g_macTimerEvt["));

	free(header);
}

static void test_mac_trx_uses_typed_callback_and_fields(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/mac/mac_trx.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "mac_timer_evt_t g_macTimerEvt;"));
	EXPECT_TRUE(contains(source, "static inline void timer_evt_cb_set(ev_timer_callback_t cb)"));
	EXPECT_TRUE(contains(source, "static inline ev_timer_callback_t timer_evt_cb_get(void)"));
	EXPECT_TRUE(contains_between(source,
				     "static inline void timer_evt_cb_set(ev_timer_callback_t cb)",
				     "static inline ev_timer_callback_t timer_evt_cb_get(void)",
				     "g_macTimerEvt.cb = cb;"));
	EXPECT_TRUE(contains_between(source,
				     "static inline ev_timer_callback_t timer_evt_cb_get(void)",
				     "static inline void timer_evt_deadline_set(u32 tick)",
				     "return g_macTimerEvt.cb;"));
	EXPECT_TRUE(contains_between(source,
				     "static inline void timer_evt_deadline_set(u32 tick)",
				     "static inline u32 timer_evt_deadline_get(void)",
				     "g_macTimerEvt.deadline = tick;"));
	EXPECT_TRUE(contains_between(source,
				     "static inline u32 timer_evt_deadline_get(void)",
				     "static inline u8 timer_evt_state_get(void)",
				     "return g_macTimerEvt.deadline;"));
	EXPECT_TRUE(contains_between(source,
				     "static inline u8 timer_evt_state_get(void)",
				     "static inline void timer_evt_state_set(u8 state)",
				     "return g_macTimerEvt.state;"));
	EXPECT_TRUE(contains_between(source,
				     "static inline void timer_evt_state_set(u8 state)",
				     "static inline void *mac_trx_cur_get(void)",
				     "g_macTimerEvt.state = state;"));
	EXPECT_TRUE(contains_between(source,
				     "void zb_macTimerEventProc(void *arg)",
				     "u8 mac_data_pending(void)",
				     "ev_timer_callback_t cb = timer_evt_cb_get();"));
	EXPECT_TRUE(contains_between(source,
				     "void zb_macTimerEventProc(void *arg)",
				     "u8 mac_data_pending(void)",
				     "(void)cb(NULL);"));
	EXPECT_FALSE(contains(source, "memcpy(g_macTimerEvt +"));
	EXPECT_FALSE(contains(source, "memcpy(&cb, g_macTimerEvt +"));
	EXPECT_FALSE(contains(source, "g_macTimerEvt[MAC_TIMER_EVT_"));

	free(source);
}

int main(void)
{
	test_mac_internal_exposes_typed_timer_state();
	test_mac_trx_uses_typed_callback_and_fields();

	if (failures != 0) {
		fprintf(stderr, "zigbee_mac_timer_event_contract: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_mac_timer_event_contract: PASS\n");
	return 0;
}
