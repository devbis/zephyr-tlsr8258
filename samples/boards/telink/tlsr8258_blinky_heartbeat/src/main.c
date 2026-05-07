/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define SLEEP_TIME_MS 100
#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

volatile uint32_t tlsr_blinky_marker;
volatile uint32_t tlsr_blinky_loop_count;
volatile uint32_t tlsr_blinky_toggle_count;
volatile uint32_t tlsr_blinky_printf_count;
volatile uint32_t tlsr_blinky_sleep_count;
volatile int32_t tlsr_blinky_last_ret;

static FUNC_NORETURN void park_with_marker(uint32_t marker)
{
	tlsr_blinky_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

int main(void)
{
	bool led_state = true;
	int ret;

	tlsr_blinky_marker = 0x82583001u;

	if (!gpio_is_ready_dt(&led)) {
		park_with_marker(0x8258e301u);
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	tlsr_blinky_last_ret = ret;
	if (ret < 0) {
		park_with_marker(0x8258e302u);
	}

	tlsr_blinky_marker = 0x82583002u;

	for (;;) {
		tlsr_blinky_loop_count++;

		ret = gpio_pin_toggle_dt(&led);
		tlsr_blinky_last_ret = ret;
		if (ret < 0) {
			park_with_marker(0x8258e303u);
		}
		tlsr_blinky_toggle_count++;

		led_state = !led_state;
		printf("LED state: %s loop=%u\n",
		       led_state ? "ON" : "OFF", tlsr_blinky_loop_count);
		tlsr_blinky_printf_count++;

		k_msleep(SLEEP_TIME_MS);
		tlsr_blinky_sleep_count++;
		tlsr_blinky_marker = 0x82583000u;
	}
}

