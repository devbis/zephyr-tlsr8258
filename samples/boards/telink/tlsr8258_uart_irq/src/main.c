/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const uint8_t tx_data[] = "tlsr8258 uart irq\r\n";

volatile uint32_t tlsr_uart_irq_marker;
volatile uint32_t tlsr_uart_irq_count;
volatile uint32_t tlsr_uart_irq_tx_pos;
volatile uint32_t tlsr_uart_irq_rx_count;

static FUNC_NORETURN void park(uint32_t marker)
{
	tlsr_uart_irq_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

static void uart_irq_cb(const struct device *dev, void *user_data)
{
	uint8_t rx_buf[4];

	ARG_UNUSED(user_data);
	tlsr_uart_irq_count++;

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		int got = uart_fifo_read(dev, rx_buf, sizeof(rx_buf));

		if (got <= 0) {
			break;
		}
		tlsr_uart_irq_rx_count += got;
	}

	if (uart_irq_tx_ready(dev)) {
		int sent = uart_fifo_fill(dev, &tx_data[tlsr_uart_irq_tx_pos],
					  sizeof(tx_data) - 1u - tlsr_uart_irq_tx_pos);

		if (sent > 0) {
			tlsr_uart_irq_tx_pos += sent;
		}

		if (tlsr_uart_irq_tx_pos >= sizeof(tx_data) - 1u) {
			uart_irq_tx_disable(dev);
			tlsr_uart_irq_marker = 0x82580000u;
		}
	}
}

int main(void)
{
	tlsr_uart_irq_marker = 0x82585000u;

	if (!device_is_ready(uart_dev)) {
		park(0x8258e501u);
	}

	if (uart_irq_callback_user_data_set(uart_dev, uart_irq_cb, NULL) != 0) {
		park(0x8258e502u);
	}

	uart_irq_rx_enable(uart_dev);
	uart_irq_tx_enable(uart_dev);

	for (uint32_t i = 0u; i < 1000000u; i++) {
		if (tlsr_uart_irq_marker == 0x82580000u) {
			park(0x82580000u);
		}
		compiler_barrier();
	}

	park(0x8258e503u);
}
