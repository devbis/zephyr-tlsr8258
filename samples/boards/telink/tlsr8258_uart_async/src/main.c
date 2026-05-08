/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const uint8_t tx_data[] = "tlsr8258 uart async\r\n";

volatile uint32_t tlsr_uart_async_marker;
volatile uint32_t tlsr_uart_async_event_count;
volatile uint32_t tlsr_uart_async_tx_done_len;

static FUNC_NORETURN void park(uint32_t marker)
{
	tlsr_uart_async_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

static void uart_async_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	tlsr_uart_async_event_count++;

	if (evt->type == UART_TX_DONE) {
		tlsr_uart_async_tx_done_len = evt->data.tx.len;
		tlsr_uart_async_marker = 0x82580000u;
	} else if (evt->type == UART_TX_ABORTED) {
		park(0x8258e602u);
	}
}

int main(void)
{
	int ret;

	tlsr_uart_async_marker = 0x82586000u;

	if (!device_is_ready(uart_dev)) {
		park(0x8258e601u);
	}

	ret = uart_callback_set(uart_dev, uart_async_cb, NULL);
	if (ret != 0) {
		park(0x8258e603u);
	}

	ret = uart_tx(uart_dev, tx_data, sizeof(tx_data) - 1u, SYS_FOREVER_US);
	if (ret != 0) {
		park(0x8258e604u);
	}

	for (uint32_t i = 0u; i < 1000000u; i++) {
		if (tlsr_uart_async_marker == 0x82580000u) {
			park(0x82580000u);
		}
		compiler_barrier();
	}

	park(0x8258e605u);
}
