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

static void test_request_key_task_records_destination_and_send_status(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api_zdo_send_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "u32 zb_request_key_trace[4] = {0x41505251U, 0U, 0U, 0U};"));
	EXPECT_TRUE(contains(source, "zb_request_key_trace[1] = ((u32)nwkDst << 16) | macDst;"));
	EXPECT_TRUE(contains(source, "zb_request_key_trace[2] = ((u32)req->keyType << 24) |"));
	EXPECT_TRUE(contains(source, "zb_request_key_trace[3] = 0xa9b10000U | (u16)sendStatus;"));

	free(source);
}

static void test_request_key_send_uses_vendor_style_aps_security(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api_zdo_send_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "buf[idx++] = 0x41U;"));
	EXPECT_TRUE(contains(source, "idx += ss_apsEnAuxHdrFill(&buf[idx], (void *)payload, 0);"));
	EXPECT_TRUE(contains(source, "if (ss_keyHash(&pad, (u8 *)linkKey, keyHash) != RET_OK)"));
	EXPECT_TRUE(contains(source, "apsNonce[12] = frame[apsHdrIdx + 2U];"));
	EXPECT_TRUE(contains(source, "frameCounter = BUILD_U32(frame[apsHdrIdx + 3U],"));
	EXPECT_TRUE(contains(source, "nv_nwkFrameCountSaveToFlash(ss_ib.outgoingFrameCounter);"));
	EXPECT_TRUE(contains(source, "u8 pad = 0U;"));
	EXPECT_TRUE(contains(source, "u8 keyHash[SEC_KEY_LEN];"));
	EXPECT_TRUE(contains(source, "const u8 *apsKey;"));
	EXPECT_TRUE(contains(source, "case SS_SECUR_KEY_LOAD_KEY:"));
	EXPECT_TRUE(contains(source, "pad = 2U;"));
	EXPECT_TRUE(contains(source, "frame[apsHdrIdx] |= 0x20U;"));

	EXPECT_TRUE(!contains(source, "buf[idx++] = 0x21U;"));
	EXPECT_TRUE(!contains(source, "nonce[12] = ZB_MINIMAL_APS_SEC_CTRL;"));
	EXPECT_TRUE(!contains(source, "keyPair.outgoingFrameCounter"));
	EXPECT_TRUE(!contains(source, "zb_minimal_dev_key_pair_save(&keyPair);"));

	free(source);
}

static void test_request_key_send_uses_vendor_style_nwk_security(void)
{
	char *source = read_file(WORKTREE_ROOT "/subsys/zigbee/zbapi/zb_api_zdo_send_minimal.c");

	EXPECT_TRUE(source != NULL);
	if (source == NULL) {
		return;
	}

	EXPECT_TRUE(contains(source, "u8 *nwkKey;"));
	EXPECT_TRUE(contains(source, "u32 nwkFrameCounter = 0U;"));
	EXPECT_TRUE(contains(source, "nwkKey = zb_minimal_active_nwk_key_get();"));
	EXPECT_TRUE(contains(source, "if (nwkKey == NULL || ss_ib.securityLevel == 0U) {"));
	EXPECT_TRUE(contains(source, "nwkHdrIdx = idx;"));
	EXPECT_TRUE(contains(source, "nwkHdrLen = zb_minimal_build_nwk_header(&frame[idx], nwkDst, 30U, TRUE,"));
	EXPECT_TRUE(contains(source, "idx += nwkHdrLen;"));
	EXPECT_TRUE(contains(source, "COPY_U32TOBUFFER(&nonce[8], nwkFrameCounter);"));
	EXPECT_TRUE(contains(source, "nonce[12] = ZB_MINIMAL_NWK_SEC_CTRL;"));
	EXPECT_TRUE(contains(source, "enc_len = zb_minimal_ccm_encrypt_auth(nwkKey, nonce, ZB_MINIMAL_NWK_MIC_LEN,"));
	EXPECT_TRUE(contains(source, "frame[nwkHdrIdx + 8U] = ZB_MINIMAL_NWK_SEC_CTRL_WIRE;"));

	EXPECT_TRUE(!contains(source, "zb_minimal_build_nwk_header(&frame[idx], nwkDst, 30U, FALSE, NULL);"));

	free(source);
}

int main(void)
{
	test_request_key_task_records_destination_and_send_status();
	test_request_key_send_uses_vendor_style_aps_security();
	test_request_key_send_uses_vendor_style_nwk_security();

	if (failures != 0) {
		fprintf(stderr, "zigbee_request_key_send_trace: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee_request_key_send_trace: PASS\n");
	return 0;
}
