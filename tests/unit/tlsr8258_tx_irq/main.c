#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ieee802154_tlsr8258_tx_irq.h"

#define RF_IRQ_TX BIT(1)
#define RF_IRQ_CMD_DONE BIT(5)
#define RF_IRQ_FSM_TIMEOUT BIT(6)
#define RF_IRQ_TX_DS BIT(8)
#define RF_IRQ_STX_TIMEOUT BIT(12)

#define BIT(n) (1u << (n))

static int failures;

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		printf("FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		printf("FAIL %s:%d expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static void test_cmd_done_alone_is_not_success(void)
{
	EXPECT_FALSE(tlsr8258_tx_irq_indicates_success(RF_IRQ_CMD_DONE));
}

static void test_tx_irq_is_success(void)
{
	EXPECT_TRUE(tlsr8258_tx_irq_indicates_success(RF_IRQ_TX));
}

static void test_tx_ds_irq_is_success(void)
{
	EXPECT_TRUE(tlsr8258_tx_irq_indicates_success(RF_IRQ_TX_DS));
}

static void test_timeout_irqs_are_not_success(void)
{
	EXPECT_FALSE(tlsr8258_tx_irq_indicates_success(RF_IRQ_STX_TIMEOUT));
	EXPECT_FALSE(tlsr8258_tx_irq_indicates_success(RF_IRQ_FSM_TIMEOUT));
	EXPECT_FALSE(tlsr8258_tx_irq_indicates_success(RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT));
}

static void test_session_mask_keeps_tx_ds_for_regular_tx(void)
{
	uint16_t mask = RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT;

	EXPECT_TRUE(tlsr8258_tx_irq_session_mask(mask, false) == mask);
}

static void test_session_mask_drops_tx_ds_for_post_tx_followup_candidate(void)
{
	uint16_t mask = RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT;

	EXPECT_TRUE(tlsr8258_tx_irq_session_mask(mask, true) ==
		    (uint16_t)(RF_IRQ_TX | RF_IRQ_STX_TIMEOUT));
}

static void test_start_clear_mask_keeps_full_clear_for_regular_tx(void)
{
	EXPECT_TRUE(tlsr8258_tx_irq_start_clear_mask(false) == 0xffffu);
}

static void test_start_clear_mask_matches_vendor_poll_tx_contract(void)
{
	EXPECT_TRUE(tlsr8258_tx_irq_start_clear_mask(true) == (uint16_t)(BIT(0) | BIT(1)));
}

static void test_force_manual_off_never_stops_state_machine(void)
{
	/*
	 * force_manual_off used to write 0x0f00 = 0x80 (the RF-OFF command)
	 * before an association poll and never restart the state machine, so the
	 * radio was OFF for the whole post-poll window and missed the AssocResp.
	 * It must now be a no-op for BOTH cases: the poll uses the same manual TX
	 * path as every other frame.
	 */
	EXPECT_FALSE(tlsr8258_tx_force_manual_off_before_start(false));
	EXPECT_FALSE(tlsr8258_tx_force_manual_off_before_start(true));
}

static void test_rx_rearm_needed_for_isrless_poll_completion(void)
{
	/* The association poll (followup expected) that completes outside the
	 * RF ISR must re-arm the RX DMA buffer for the AssocResp. */
	EXPECT_TRUE(tlsr8258_tx_poll_needs_rx_rearm(true, false));
}

static void test_rx_rearm_not_needed_when_isr_handled_the_swap(void)
{
	/* If the RF ISR handled the completion it already swapped buffers. */
	EXPECT_FALSE(tlsr8258_tx_poll_needs_rx_rearm(true, true));
}

static void test_rx_rearm_not_needed_for_plain_tx(void)
{
	/* A regular TX (no indirect followup) never leaves the buffer occupied
	 * by an auto-received ACK in a way that blocks a follow-up frame. */
	EXPECT_FALSE(tlsr8258_tx_poll_needs_rx_rearm(false, false));
	EXPECT_FALSE(tlsr8258_tx_poll_needs_rx_rearm(false, true));
}

static void test_isrless_poll_success_path_rearms_rx_dma_buffer(void)
{
	/*
	 * Source contract: the tx() status-poll completion path (which is how
	 * the router's association poll finishes, since TX_DS is masked for the
	 * session) must re-arm the RX DMA buffer via the predicate. Guards
	 * against silently dropping the fix that lets the AssocResp DMA in.
	 */
	FILE *fp;
	long file_size;
	char *buffer;
	bool found_rearm = false;

	fp = fopen(WORKTREE_ROOT "/drivers/ieee802154/ieee802154_tlsr8258.c", "rb");
	if (fp == NULL) {
		printf("FAIL %s:%d unable to open tlsr8258 driver\n", __FILE__, __LINE__);
		failures++;
		return;
	}
	if (fseek(fp, 0, SEEK_END) != 0 || (file_size = ftell(fp)) < 0 ||
	    fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		printf("FAIL %s:%d unable to size tlsr8258 driver\n", __FILE__, __LINE__);
		failures++;
		return;
	}
	buffer = malloc((size_t)file_size + 1u);
	if (buffer == NULL) {
		fclose(fp);
		printf("FAIL %s:%d unable to allocate buffer\n", __FILE__, __LINE__);
		failures++;
		return;
	}
	if (fread(buffer, 1u, (size_t)file_size, fp) != (size_t)file_size) {
		fclose(fp);
		free(buffer);
		printf("FAIL %s:%d unable to read tlsr8258 driver\n", __FILE__, __LINE__);
		failures++;
		return;
	}
	fclose(fp);
	buffer[file_size] = '\0';
	found_rearm = strstr(buffer, "tlsr8258_tx_poll_needs_rx_rearm(expect_post_tx_followup") != NULL;
	free(buffer);
	EXPECT_TRUE(found_rearm);
}

static void test_vendor_poll_irq_mask_excludes_tx_ds_from_global_runtime_enable(void)
{
	FILE *fp;
	long file_size;
	char *buffer;
	bool found_tx_ds_enable = false;

	fp = fopen(WORKTREE_ROOT "/drivers/ieee802154/ieee802154_tlsr8258.c", "rb");
	if (fp == NULL) {
		printf("FAIL %s:%d unable to open tlsr8258 driver\n", __FILE__, __LINE__);
		failures++;
		return;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		printf("FAIL %s:%d unable to size tlsr8258 driver\n", __FILE__, __LINE__);
		failures++;
		return;
	}

	file_size = ftell(fp);
	if ((file_size < 0) || (fseek(fp, 0, SEEK_SET) != 0)) {
		fclose(fp);
		printf("FAIL %s:%d unable to rewind tlsr8258 driver\n", __FILE__, __LINE__);
		failures++;
		return;
	}

	buffer = malloc((size_t)file_size + 1u);
	if (buffer == NULL) {
		fclose(fp);
		printf("FAIL %s:%d unable to allocate buffer\n", __FILE__, __LINE__);
		failures++;
		return;
	}

	if (fread(buffer, 1u, (size_t)file_size, fp) != (size_t)file_size) {
		fclose(fp);
		free(buffer);
		printf("FAIL %s:%d unable to read tlsr8258 driver\n", __FILE__, __LINE__);
		failures++;
		return;
	}
	fclose(fp);
	buffer[file_size] = '\0';
	found_tx_ds_enable =
		strstr(buffer, "RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT") != NULL;
	free(buffer);
	EXPECT_FALSE(found_tx_ds_enable);
}

int main(void)
{
	test_cmd_done_alone_is_not_success();
	test_tx_irq_is_success();
	test_tx_ds_irq_is_success();
	test_timeout_irqs_are_not_success();
	test_session_mask_keeps_tx_ds_for_regular_tx();
	test_session_mask_drops_tx_ds_for_post_tx_followup_candidate();
	test_start_clear_mask_keeps_full_clear_for_regular_tx();
	test_start_clear_mask_matches_vendor_poll_tx_contract();
	test_force_manual_off_never_stops_state_machine();
	test_vendor_poll_irq_mask_excludes_tx_ds_from_global_runtime_enable();
	test_rx_rearm_needed_for_isrless_poll_completion();
	test_rx_rearm_not_needed_when_isr_handled_the_swap();
	test_rx_rearm_not_needed_for_plain_tx();
	test_isrless_poll_success_path_rearms_rx_dma_buffer();

	if (failures != 0) {
		printf("tlsr8258_tx_irq: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_tx_irq: PASS\n");
	return 0;
}
