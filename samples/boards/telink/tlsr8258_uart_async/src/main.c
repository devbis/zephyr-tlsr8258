/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const uint8_t tx_data[] = "tlsr8258 uart async\r\n";
static uint8_t rx_data[sizeof(tx_data) - 1u];

volatile uint32_t tlsr_uart_async_marker;
volatile uint32_t tlsr_uart_async_event_count;
volatile uint32_t tlsr_uart_async_tx_done_len;
volatile uint32_t tlsr_uart_async_rx_done_len;
volatile uint32_t tlsr_uart_async_rx_mismatch;

static volatile bool tx_done;
static volatile bool rx_done;

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
		tx_done = true;
	} else if (evt->type == UART_TX_ABORTED) {
		park(0x8258e602u);
	} else if (evt->type == UART_RX_RDY) {
		size_t offset = evt->data.rx.offset;
		size_t len = evt->data.rx.len;

		tlsr_uart_async_rx_done_len += len;
		if (offset + len > sizeof(rx_data) ||
		    memcmp(&rx_data[offset], &tx_data[offset], len) != 0) {
			tlsr_uart_async_rx_mismatch++;
			park(0x8258e606u);
		}
		if (tlsr_uart_async_rx_done_len == sizeof(tx_data) - 1u) {
			rx_done = true;
		}
	} else if (evt->type == UART_RX_DISABLED &&
		   tlsr_uart_async_rx_done_len != sizeof(tx_data) - 1u) {
		park(0x8258e607u);
	}

	if (tx_done && rx_done) {
		tlsr_uart_async_marker = 0x82580000u;
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

	ret = uart_rx_enable(uart_dev, rx_data, sizeof(rx_data), SYS_FOREVER_US);
	if (ret != 0) {
		park(0x8258e608u);
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
