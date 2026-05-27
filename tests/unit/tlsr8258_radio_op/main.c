#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ieee802154_tlsr8258_radio_op.h"

static int failures;

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		printf("FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_EQ(actual, expected) do { \
	int _actual = (int)(actual); \
	int _expected = (int)(expected); \
	if (_actual != _expected) { \
		printf("FAIL %s:%d %s=%d expected %d\n", __FILE__, __LINE__, #actual, _actual, \
		       _expected); \
		failures++; \
	} \
} while (0)

static void test_tx_success_with_post_rx_enters_waiting_state(void)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);

	EXPECT_TRUE(tlsr8258_radio_op_on_tx_success(&op) == false);
	EXPECT_EQ(op.state, TLSR8258_RADIO_OP_WAITING_POST_TX_RX);
}

static void test_ack_without_pending_completes_operation(void)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);
	(void)tlsr8258_radio_op_on_tx_success(&op);

	EXPECT_TRUE(tlsr8258_radio_op_on_rx(&op, true, false, false));
	EXPECT_EQ(tlsr8258_radio_op_result_errno(&op), 0);
}

static void test_timeout_maps_to_eagain(void)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, false, true);
	(void)tlsr8258_radio_op_on_tx_success(&op);
	tlsr8258_radio_op_on_timeout(&op);

	EXPECT_EQ(op.state, TLSR8258_RADIO_OP_COMPLETE_NO_RX);
	EXPECT_EQ(tlsr8258_radio_op_result_errno(&op), -EAGAIN);
}

int main(void)
{
	test_tx_success_with_post_rx_enters_waiting_state();
	test_ack_without_pending_completes_operation();
	test_timeout_maps_to_eagain();

	if (failures != 0) {
		printf("tlsr8258_radio_op: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_radio_op: PASS\n");
	return 0;
}
