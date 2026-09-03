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

static void test_request_key_api_uses_native_apsme_request(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "u8 zb_apsmeRequestKeyReq(ss_apsmeRequestKeyReq_t *req)"));
	EXPECT_TRUE(contains(source, "buf = zb_buf_allocate();"));
	EXPECT_TRUE(contains(source, "tl_zbTaskPost(ss_apsmeRequestKeyReq, buf);"));
	EXPECT_TRUE(contains(source, "return RET_NO_MEMORY;"));
	free(source);
}

static void test_request_key_serialization_uses_vendor_zdo_request_path(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "static zdo_status_t zb_zdo_send_short_req"));
	EXPECT_TRUE(contains(source, "req.zdoRspReceivedIndCb = ind_cb;"));
	EXPECT_TRUE(contains(source, "return (zdo_status_t)zdo_send_req(&req);"));
	EXPECT_FALSE(contains(source, "zb_minimal_ccm_encrypt_auth"));
	free(source);
}

static void test_request_key_security_is_owned_by_ss_apsme(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/ss/ss_apsEnDecrypt.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}
	EXPECT_TRUE(contains(source, "ss_keyHash(&pad, key, keyTemp)"));
	EXPECT_TRUE(contains(source, "SS_SECUR_KEY_TRANSPORT_KEY"));
	EXPECT_TRUE(contains(source, "SS_SECUR_KEY_LOAD_KEY"));
	free(source);
}

int main(void)
{
	test_request_key_api_uses_native_apsme_request();
	test_request_key_serialization_uses_vendor_zdo_request_path();
	test_request_key_security_is_owned_by_ss_apsme();

	if (failures != 0) {
		fprintf(stderr, "zigbee_request_key_send_trace: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_request_key_send_trace: PASS\n");
	return 0;
}
