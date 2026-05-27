#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define RF_IRQ_TX BIT(1)
#define RF_IRQ_CMD_DONE BIT(5)
#define RF_IRQ_FSM_TIMEOUT BIT(6)
#define RF_IRQ_TX_DS BIT(8)
#define RF_IRQ_STX_TIMEOUT BIT(11)

#define BIT(n) (1u << (n))

bool tlsr8258_tx_irq_indicates_success(uint16_t irq);

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

int main(void)
{
	test_cmd_done_alone_is_not_success();
	test_tx_irq_is_success();
	test_tx_ds_irq_is_success();
	test_timeout_irqs_are_not_success();

	if (failures != 0) {
		printf("tlsr8258_tx_irq: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_tx_irq: PASS\n");
	return 0;
}
