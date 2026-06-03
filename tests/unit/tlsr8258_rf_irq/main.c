#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT(n) (1u << (n))

#define RF_IRQ_RX       BIT(0)
#define RF_IRQ_TX       BIT(1)
#define RF_IRQ_RX_CRC_2 BIT(4)
#define RF_IRQ_RX_DR    BIT(9)
#define RF_IRQ_RX_EVENTS (RF_IRQ_RX | RF_IRQ_RX_CRC_2 | RF_IRQ_RX_DR)

uint16_t tlsr8258_rf_irq_runtime_mask(void);
bool tlsr8258_rf_irq_has_rx_event(uint16_t irq);
uint16_t tlsr8258_rf_irq_effective_status(uint16_t irq, const uint8_t *rx, size_t rx_size);

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
		goto out;
	}

	size = ftell(fp);
	if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
		goto out;
	}

	buffer = malloc((size_t)size + 1u);
	if (buffer == NULL) {
		goto out;
	}

	if (fread(buffer, 1u, (size_t)size, fp) == (size_t)size) {
		buffer[size] = '\0';
		found = strstr(buffer, needle) != NULL;
	}

	free(buffer);

out:
	fclose(fp);
	return found;
}

#define EXPECT_FILE_CONTAINS(path, needle) do { \
	if (!file_contains((path), (needle))) { \
		fprintf(stderr, "FAIL %s:%d expected %s to contain %s\n", __FILE__, __LINE__, \
			(path), (needle)); \
		failures++; \
	} \
} while (0)

#define EXPECT_FILE_NOT_CONTAINS(path, needle) do { \
	if (file_contains((path), (needle))) { \
		fprintf(stderr, "FAIL %s:%d expected %s to not contain %s\n", __FILE__, __LINE__, \
			(path), (needle)); \
		failures++; \
	} \
} while (0)

#define WORKTREE_FILE(relpath) WORKTREE_ROOT "/" relpath

#define EXPECT_EQ(actual, expected) do { \
	unsigned int _actual = (unsigned int)(actual); \
	unsigned int _expected = (unsigned int)(expected); \
	if (_actual != _expected) { \
		fprintf(stderr, "FAIL %s:%d: %s=0x%x expected 0x%x\n", __FILE__, __LINE__, \
			#actual, _actual, _expected); \
		failures++; \
	} \
} while (0)

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

static void test_runtime_irq_mask_matches_runtime_rx_event_contract(void)
{
	EXPECT_EQ(tlsr8258_rf_irq_runtime_mask(), RF_IRQ_RX_EVENTS | RF_IRQ_TX);
}

static void test_rx_event_accepts_all_vendor_rx_indicators(void)
{
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX));
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX | RF_IRQ_RX_CRC_2));
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2));
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_DR));
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2 | RF_IRQ_RX_DR));
}

static void test_zero_irq_with_valid_dma_rx_synthesizes_rx_status(void)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 4u;
	dma[16] = 0x10u;

	EXPECT_EQ(tlsr8258_rf_irq_effective_status(0u, dma, sizeof(dma)), RF_IRQ_RX);
}

static void test_secondary_rx_irq_with_valid_dma_promotes_to_logical_rx(void)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 4u;
	dma[16] = 0x10u;

	EXPECT_EQ(tlsr8258_rf_irq_effective_status(RF_IRQ_RX_DR, dma, sizeof(dma)), RF_IRQ_RX);
	EXPECT_EQ(tlsr8258_rf_irq_effective_status(RF_IRQ_RX_CRC_2, dma, sizeof(dma)),
		  RF_IRQ_RX);
}

static void test_zero_irq_with_invalid_dma_does_not_synthesize_rx_status(void)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 5u;
	dma[16] = 0x00u;

	EXPECT_EQ(tlsr8258_rf_irq_effective_status(0u, dma, sizeof(dma)), 0u);
}

static void test_non_rx_irq_bits_do_not_trigger_rx_capture(void)
{
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_TX));
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_TX | RF_IRQ_RX_DR));
}

static void test_invalid_dma_clears_rx_event_bits_but_preserves_other_irqs(void)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 5u;
	dma[16] = 0x00u;

	EXPECT_EQ(tlsr8258_rf_irq_effective_status(RF_IRQ_TX | RF_IRQ_RX_DR, dma, sizeof(dma)),
		  RF_IRQ_TX);
}

static void test_public_bridge_header_exposes_sink_only_registration(void)
{
	const char *path = WORKTREE_FILE("include/zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h");

	EXPECT_FILE_CONTAINS(path, "#include <zephyr/zigbee/zb_radio_port.h>");
	EXPECT_FILE_CONTAINS(path, "typedef zb_radio_port_rx_sink_t tlsr8258_zigbee_rx_sink_t;");
	EXPECT_FILE_CONTAINS(path, "void tlsr8258_zigbee_register_rx_sink");
	EXPECT_FILE_NOT_CONTAINS(path, "struct tlsr8258_rx_frame_view {");
	EXPECT_FILE_NOT_CONTAINS(path, "tlsr8258_zigbee_rx_cb_t");
	EXPECT_FILE_NOT_CONTAINS(path, "tlsr8258_zigbee_register_rx_cb");
}

static void test_zigbee_port_header_exposes_sink_only_registration(void)
{
	const char *path = WORKTREE_FILE("subsys/zigbee/include/zephyr/zigbee/zb_radio_port.h");

	EXPECT_FILE_CONTAINS(path, "struct tlsr8258_rx_frame_view {");
	EXPECT_FILE_CONTAINS(path, "void zb_radio_port_register_rx_sink");
	EXPECT_FILE_CONTAINS(path, "typedef int (*zb_radio_port_rx_sink_t)(const struct tlsr8258_rx_frame_view *frame)");
	EXPECT_FILE_NOT_CONTAINS(path, "struct zb_radio_port_rx_frame");
	EXPECT_FILE_NOT_CONTAINS(path, "zb_radio_port_rx_cb_t");
	EXPECT_FILE_NOT_CONTAINS(path, "zb_radio_port_register_rx_cb");
}

static void test_zigbee_driver_registers_sink_api(void)
{
	const char *path = WORKTREE_FILE("subsys/zigbee/platform/zephyr/drv_radio_zephyr.c");

	EXPECT_FILE_CONTAINS(path, "zb_radio_port_register_rx_sink(");
	EXPECT_FILE_NOT_CONTAINS(path, "rf_rx_irq_handler(");
	EXPECT_FILE_NOT_CONTAINS(path, "rf_tx_irq_handler(");
	EXPECT_FILE_NOT_CONTAINS(path, "rf_rxBuf");
	EXPECT_FILE_NOT_CONTAINS(path, "tlsr8258_zigbee_bridge.h");
	EXPECT_FILE_NOT_CONTAINS(path, "struct zb_radio_rx_slot {");
	EXPECT_FILE_NOT_CONTAINS(path, "g_radio_rx_work");
	EXPECT_FILE_NOT_CONTAINS(path, "rx_pending_count");
	EXPECT_FILE_NOT_CONTAINS(path, "zb_radio_rx_slot_alloc(");
	EXPECT_FILE_NOT_CONTAINS(path, "struct tlsr8258_rx_frame_view {");
	EXPECT_FILE_NOT_CONTAINS(path, "zb_radio_port_register_rx_cb(");
}

static void test_tlsr8258_dispatch_uses_sink_as_authoritative_path(void)
{
	const char *path = WORKTREE_FILE("drivers/ieee802154/ieee802154_tlsr8258.c");

	EXPECT_FILE_CONTAINS(path, "void tlsr8258_zigbee_register_rx_sink");
	EXPECT_FILE_NOT_CONTAINS(path, "void tlsr8258_zigbee_register_rx_cb");
	EXPECT_FILE_CONTAINS(path, "if (tlsr8258_zigbee_rx_sink != NULL)");
	EXPECT_FILE_CONTAINS(path, "rc = tlsr8258_zigbee_rx_sink(&view);");
	EXPECT_FILE_CONTAINS(path, "if (rc < 0)");
	EXPECT_FILE_CONTAINS(path, "static void tlsr8258_rx_capture_common(");
	EXPECT_FILE_NOT_CONTAINS(path, "static void tlsr8258_rx_isr(");
	EXPECT_FILE_CONTAINS(path, "uint16_t rx_ack = irq_status & RF_IRQ_RX_EVENTS;");
	EXPECT_FILE_CONTAINS(path, "if (rx_ack == 0u)");
}

static void test_rf_isr_signals_tx_success_via_radio_op_and_sem(void)
{
	const char *path = WORKTREE_FILE("drivers/ieee802154/ieee802154_tlsr8258.c");

	EXPECT_FILE_CONTAINS(path, "else if ((effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS)) != 0u) {");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_radio_tx_count_inc();");
	EXPECT_FILE_CONTAINS(path, "tx_complete = (tlsr8258_radio.op.state == TLSR8258_RADIO_OP_TX_PENDING) &&");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_radio_op_on_tx_success(&tlsr8258_radio.op);");
	EXPECT_FILE_CONTAINS(path, "k_sem_give(&tlsr8258_tx_wait);");
}

static void test_rf_isr_signals_tx_error_via_radio_op_and_sem(void)
{
	const char *path = WORKTREE_FILE("drivers/ieee802154/ieee802154_tlsr8258.c");

	EXPECT_FILE_CONTAINS(path,
		"else if ((effective_irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_radio_op_on_tx_error(&tlsr8258_radio.op, -EIO);");
	EXPECT_FILE_CONTAINS(path, "k_sem_give(&tlsr8258_tx_wait);");
}

static void test_rx_worker_completes_post_tx_rx_via_radio_op_and_sem(void)
{
	const char *path = WORKTREE_FILE("drivers/ieee802154/ieee802154_tlsr8258.c");

	EXPECT_FILE_CONTAINS(path, "const uint8_t *psdu;");
	EXPECT_FILE_CONTAINS(path, "bool is_ack;");
	EXPECT_FILE_CONTAINS(path, "bool ack_pending;");
	EXPECT_FILE_CONTAINS(path, "bool is_pending_response;");
	EXPECT_FILE_CONTAINS(path, "psdu = &frame.dma[TLSR8258_PAYLOAD_OFFSET];");
	EXPECT_FILE_CONTAINS(path, "psdu_len = frame.dma[4];");
	EXPECT_FILE_CONTAINS(path, "is_ack = tlsr8258_psdu_is_ack_for_seq(psdu, psdu_len,");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_radio.op.tx_seq);");
	EXPECT_FILE_CONTAINS(path, "ack_pending = is_ack && ((psdu[0] & TLSR8258_FRAME_PENDING) != 0u);");
	EXPECT_FILE_CONTAINS(path,
			     "is_pending_response = tlsr8258_psdu_is_pending_response(");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_radio.op.state == TLSR8258_RADIO_OP_WAITING_POST_TX_RX");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_radio_op_on_rx(&tlsr8258_radio.op, is_ack,");
	EXPECT_FILE_CONTAINS(path, "ack_pending,");
	EXPECT_FILE_CONTAINS(path, "is_pending_response);");
	EXPECT_FILE_CONTAINS(path, "k_sem_give(&tlsr8258_tx_wait);");
}

static void test_pending_response_classification_uses_shared_helper(void)
{
	const char *path = WORKTREE_FILE("drivers/ieee802154/ieee802154_tlsr8258.c");

	EXPECT_FILE_CONTAINS(path, "static bool tlsr8258_psdu_is_pending_response(const uint8_t *psdu,");
	EXPECT_FILE_CONTAINS(path, "return has_ack_match_fields && !is_ack &&");
	EXPECT_FILE_CONTAINS(path, "rx_is_pending_response =");
	EXPECT_FILE_CONTAINS(path, "tlsr8258_psdu_is_pending_response(psdu, psdu_len, tx_seq);");
	EXPECT_FILE_CONTAINS(path,
			     "is_pending_response = tlsr8258_psdu_is_pending_response(");
	EXPECT_FILE_NOT_CONTAINS(path,
				 "rx_is_pending_response = (psdu_len >= TLSR8258_MIN_FRAME_LENGTH) &&");
}

static void test_zigbee_drv_enable_irq_reenables_global_gate(void)
{
	const char *path = WORKTREE_FILE("subsys/zigbee/platform/zephyr/drv_hw_zephyr.c");

	EXPECT_FILE_CONTAINS(path, "irq_unlock(1);");
	EXPECT_FILE_NOT_CONTAINS(path, "irq_unlock(0);");
}

static void test_zigbee_bootstrap_enables_global_irq_gate(void)
{
	const char *path = WORKTREE_FILE("subsys/zigbee/platform/zephyr/zb_main.c");

	EXPECT_FILE_CONTAINS(path, "#include \"drv_hw.h\"");
	EXPECT_FILE_CONTAINS(path, "drv_enable_irq();");
}

int main(void)
{
	test_runtime_irq_mask_matches_runtime_rx_event_contract();
	test_rx_event_accepts_all_vendor_rx_indicators();
	test_zero_irq_with_valid_dma_rx_synthesizes_rx_status();
	test_secondary_rx_irq_with_valid_dma_promotes_to_logical_rx();
	test_zero_irq_with_invalid_dma_does_not_synthesize_rx_status();
	test_non_rx_irq_bits_do_not_trigger_rx_capture();
	test_invalid_dma_clears_rx_event_bits_but_preserves_other_irqs();
	test_public_bridge_header_exposes_sink_only_registration();
	test_zigbee_port_header_exposes_sink_only_registration();
	test_zigbee_driver_registers_sink_api();
	test_tlsr8258_dispatch_uses_sink_as_authoritative_path();
	test_rf_isr_signals_tx_success_via_radio_op_and_sem();
	test_rf_isr_signals_tx_error_via_radio_op_and_sem();
	test_rx_worker_completes_post_tx_rx_via_radio_op_and_sem();
	test_pending_response_classification_uses_shared_helper();
	test_zigbee_drv_enable_irq_reenables_global_gate();
	test_zigbee_bootstrap_enables_global_irq_gate();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_rf_irq: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_rf_irq: PASS\n");
	return 0;
}
