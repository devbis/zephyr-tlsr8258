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

static bool contains_between(const char *source, const char *start_marker,
				     const char *end_marker, const char *needle)
{
	const char *start = strstr(source, start_marker);
	const char *end;
	const char *match;

	if (start == NULL) {
		return false;
	}
	end = strstr(start, end_marker);
	match = strstr(start, needle);
	return end != NULL && match != NULL && match < end;
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

static void test_node_descriptor_response_requests_transport_key(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/bdb/bdb.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(strstr(source, "static void bdb_nodeDescRespHandler(void *arg)") != NULL);
	EXPECT_TRUE(strstr(source, "if (zb_apsmeRequestKeyReq(&requestKey) == RET_OK)") != NULL);
	EXPECT_FALSE(strstr(source, "g_bdbCtx.edRuntimeReady) {") != NULL);
	free(source);
}

static void test_transport_key_decrypt_uses_vendor_security_service(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/ss/ss_apsEnDecrypt.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(strstr(source, "aux.keyIdentifer == SS_SECUR_KEY_TRANSPORT_KEY") != NULL);
	EXPECT_TRUE(strstr(source, "key = ss_ib.tcLinkKey;") != NULL);
	EXPECT_TRUE(strstr(source, "u8 pad = (aux.keyIdentifer == SS_SECUR_KEY_LOAD_KEY) ? 2U : 0U;") != NULL);
	EXPECT_TRUE(strstr(source, "ss_keyHash(&pad, key, keyTemp)") != NULL);
	free(source);
}

static void test_association_starts_native_poll_and_secure_handoff(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zdo/zdo_nwk_manager.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(strstr(source, "zdo_set_pollRate(500U);") != NULL);
	EXPECT_TRUE(strstr(source, "zdo_secure_startup_pending = true;") != NULL);
	EXPECT_TRUE(strstr(source, "void zdo_nwk_authentication_complete(void)") != NULL);
	free(source);
}

static void test_bdb_handoffs_keep_transport_key_wait_separate(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/bdb/bdb.c");
	const char *assoc = "static void bdb_ed_assoc_handoff_start(void)";
	const char *secure = "static void bdb_ed_secure_join_handoff_start(void)";

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains_between(source, assoc, secure,
				     "bdb_waitTransportKeyTimeout"));
	EXPECT_TRUE(contains_between(source, assoc, secure,
				     "TRANSPORT_NETWORK_KEY_WAIT_TIME"));
	EXPECT_TRUE(contains_between(source, secure,
				     "_CODE_BDB_ static void bdb_retrieveTcLinkKeyTimerStop(void)",
				     "bdb_retrieveTcLinkKeyTimerStop();"));
	free(source);
}

int main(void)
{
	test_node_descriptor_response_requests_transport_key();
	test_transport_key_decrypt_uses_vendor_security_service();
	test_association_starts_native_poll_and_secure_handoff();
	test_bdb_handoffs_keep_transport_key_wait_separate();

	if (failures != 0) {
		fprintf(stderr, "zigbee_transport_key_handoff: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_transport_key_handoff: PASS\n");
	return 0;
}
