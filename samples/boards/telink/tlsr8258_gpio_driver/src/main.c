/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

volatile uint32_t tlsr_gpio_driver_marker;
volatile uint32_t tlsr_gpio_driver_inputs;
volatile uint32_t tlsr_gpio_driver_outputs;
volatile uint32_t tlsr_gpio_driver_config;

static FUNC_NORETURN void park(uint32_t marker)
{
	tlsr_gpio_driver_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

int main(void)
{
	gpio_port_pins_t inputs = 0u;
	gpio_port_pins_t outputs = 0u;
	gpio_flags_t flags = 0u;
	int ret;

	tlsr_gpio_driver_marker = 0x82587000u;

	if (!gpio_is_ready_dt(&led)) {
		park(0x8258e701u);
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		park(0x8258e702u);
	}

	ret = gpio_pin_get_config(led.port, led.pin, &flags);
	if (ret != 0) {
		park(0x8258e703u);
	}

	ret = gpio_port_get_direction(led.port, BIT(led.pin), &inputs, &outputs);
	if (ret != 0) {
		park(0x8258e704u);
	}

	tlsr_gpio_driver_config = flags;
	tlsr_gpio_driver_inputs = inputs;
	tlsr_gpio_driver_outputs = outputs;

	if ((outputs & BIT(led.pin)) == 0u) {
		park(0x8258e705u);
	}

	ret = gpio_pin_set_dt(&led, 1);
	if (ret != 0) {
		park(0x8258e706u);
	}

	ret = gpio_pin_toggle_dt(&led);
	if (ret != 0) {
		park(0x8258e707u);
	}

	park(0x82580000u);
}
