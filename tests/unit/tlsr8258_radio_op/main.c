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

static void test_tx_success_without_post_rx_completes_immediately(void)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, false, false);

	EXPECT_TRUE(tlsr8258_radio_op_on_tx_success(&op));
	EXPECT_EQ(op.state, TLSR8258_RADIO_OP_COMPLETE_OK);
	EXPECT_EQ(tlsr8258_radio_op_result_errno(&op), 0);
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

static void test_ack_with_pending_waits_for_follow_up_response(void)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);
	(void)tlsr8258_radio_op_on_tx_success(&op);

	EXPECT_TRUE(!tlsr8258_radio_op_on_rx(&op, true, true, false));
	EXPECT_EQ(op.state, TLSR8258_RADIO_OP_WAITING_POST_TX_RX);
	EXPECT_TRUE(op.ack_seen);
	EXPECT_TRUE(op.ack_pending);
	EXPECT_TRUE(tlsr8258_radio_op_on_rx(&op, false, false, true));
	EXPECT_EQ(op.state, TLSR8258_RADIO_OP_COMPLETE_OK);
	EXPECT_EQ(tlsr8258_radio_op_result_errno(&op), 0);
}

static void test_tx_error_completes_with_supplied_errno(void)
{
	struct tlsr8258_radio_op op;

	tlsr8258_radio_op_reset(&op);
	tlsr8258_radio_op_prepare_tx(&op, 0x2a, true, true);
	tlsr8258_radio_op_on_tx_error(&op, -EIO);

	EXPECT_EQ(op.state, TLSR8258_RADIO_OP_COMPLETE_ERROR);
	EXPECT_EQ(tlsr8258_radio_op_result_errno(&op), -EIO);
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

static void test_build_uses_zephyr_errno_mapping(void)
{
	EXPECT_EQ(EAGAIN, 11);
	EXPECT_EQ(ENOMSG, 35);
}

int main(void)
{
	test_tx_success_with_post_rx_enters_waiting_state();
	test_tx_success_without_post_rx_completes_immediately();
	test_ack_without_pending_completes_operation();
	test_ack_with_pending_waits_for_follow_up_response();
	test_tx_error_completes_with_supplied_errno();
	test_timeout_maps_to_eagain();
	test_build_uses_zephyr_errno_mapping();

	if (failures != 0) {
		printf("tlsr8258_radio_op: %d failure(s)\n", failures);
		return 1;
	}

	printf("tlsr8258_radio_op: PASS\n");
	return 0;
}
