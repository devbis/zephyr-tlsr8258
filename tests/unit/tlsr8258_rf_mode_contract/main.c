#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static bool file_contains(const char *path, const char *needle)
{
	FILE *fp;
	long size;
	char *buffer;
	bool found = false;

	fp = fopen(path, "rb");
	if (fp == NULL) {
		fprintf(stderr, "FAIL %s:%d: unable to open %s\n", __FILE__, __LINE__, path);
		failures++;
		return false;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}

	size = ftell(fp);
	if ((size < 0) || (fseek(fp, 0, SEEK_SET) != 0)) {
		fclose(fp);
		return false;
	}

	buffer = malloc((size_t)size + 1u);
	if (buffer == NULL) {
		fclose(fp);
		return false;
	}

	if (fread(buffer, 1u, (size_t)size, fp) == (size_t)size) {
		buffer[size] = '\0';
		found = strstr(buffer, needle) != NULL;
	}

	free(buffer);
	fclose(fp);
	return found;
}

#define WORKTREE_FILE(relpath) WORKTREE_ROOT "/" relpath

#define EXPECT_FILE_CONTAINS(path, needle) do { \
	if (!file_contains((path), (needle))) { \
		fprintf(stderr, "FAIL %s:%d expected %s to contain %s\n", __FILE__, __LINE__, \
			(path), (needle)); \
		failures++; \
	} \
} while (0)

static void test_rf_mode_switches_keep_vendor_ll_mode_contract(void)
{
	const char *path = WORKTREE_FILE("drivers/ieee802154/ieee802154_tlsr8258.c");

	EXPECT_FILE_CONTAINS(path, "static void tlsr8258_rf_set_rxmode(struct tlsr8258_radio_data *radio)");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_rf_ll_mode_set(RF_LL_MODE_RX);");
	EXPECT_FILE_CONTAINS(path, "static void tlsr8258_rf_set_rxmode_fast(void)");
	EXPECT_FILE_CONTAINS(path, "static void tlsr8258_rf_set_txmode(struct tlsr8258_radio_data *radio)");
	EXPECT_FILE_CONTAINS(path, "Keep 0x0f16 fixed");
	EXPECT_FILE_CONTAINS(path, "static void tlsr8258_rf_set_txmode_for_ack(void)");
}

static void test_post_poll_followup_uses_vendor_minimal_rxmode_switch(void)
{
	/*
	 * The post-poll TX-done path must re-enter RX with the VENDOR-EXACT
	 * minimal switch (chip_8258 rf_set_rxmode: 0x428|=BIT0 then 0xf02=OFF|BIT5
	 * ONLY — no 0xf02=OFF state-machine reset, no channel/PLL reload) for a
	 * poll expecting an indirect follow-up (the association DataReq). The
	 * poll completes as plain RF_IRQ_TX (no HW auto-ACK), so the coordinator's
	 * ACK+AssocResp arrive as ordinary RX ~200us later; the reset/relock our
	 * set_rxmode/set_rxmode_fast insert on that turnaround is the suspected
	 * cause of the radio not being live when the reply lands.
	 */
	const char *path = WORKTREE_FILE("drivers/ieee802154/ieee802154_tlsr8258.c");

	EXPECT_FILE_CONTAINS(path, "if (poll_followup) {");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_rf_set_rxmode_vendor();");
	EXPECT_FILE_CONTAINS(path, "static void tlsr8258_rf_set_rxmode_vendor(void)");
}

int main(void)
{
	test_rf_mode_switches_keep_vendor_ll_mode_contract();
	test_post_poll_followup_uses_vendor_minimal_rxmode_switch();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_rf_mode_contract: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_rf_mode_contract: PASS\n");
	return 0;
}
