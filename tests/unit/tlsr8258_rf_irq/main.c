#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define BIT(n) (1u << (n))

#define RF_IRQ_RX       BIT(0)
#define RF_IRQ_TX       BIT(1)
#define RF_IRQ_RX_CRC_2 BIT(4)
#define RF_IRQ_RX_DR    BIT(9)

uint16_t tlsr8258_rf_irq_runtime_mask(void);
bool tlsr8258_rf_irq_has_rx_event(uint16_t irq);
uint16_t tlsr8258_rf_irq_effective_status(uint16_t irq, const uint8_t *rx, size_t rx_size);

static int failures;

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

static void test_runtime_irq_mask_matches_vendor_rx_tx_only(void)
{
	EXPECT_EQ(tlsr8258_rf_irq_runtime_mask(), RF_IRQ_RX | RF_IRQ_TX);
}

static void test_rx_event_requires_primary_rx_bit(void)
{
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX));
	EXPECT_TRUE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX | RF_IRQ_RX_CRC_2));
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2));
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_DR));
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2 | RF_IRQ_RX_DR));
}

static void test_zero_irq_with_valid_dma_rx_synthesizes_rx_status(void)
{
	uint8_t dma[20] = { 0 };

	dma[0] = 13u;
	dma[4] = 4u;
	dma[16] = 0x10u;

	EXPECT_EQ(tlsr8258_rf_irq_effective_status(0u, dma, sizeof(dma)), RF_IRQ_RX);
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
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_TX | RF_IRQ_RX_DR));
	EXPECT_FALSE(tlsr8258_rf_irq_has_rx_event(RF_IRQ_RX_CRC_2));
}

int main(void)
{
	test_runtime_irq_mask_matches_vendor_rx_tx_only();
	test_rx_event_requires_primary_rx_bit();
	test_zero_irq_with_valid_dma_rx_synthesizes_rx_status();
	test_zero_irq_with_invalid_dma_does_not_synthesize_rx_status();
	test_non_rx_irq_bits_do_not_trigger_rx_capture();

	if (failures != 0) {
		fprintf(stderr, "tlsr8258_rf_irq: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_rf_irq: PASS\n");
	return 0;
}
