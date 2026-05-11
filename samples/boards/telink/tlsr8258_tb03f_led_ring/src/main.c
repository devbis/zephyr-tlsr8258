/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define LED_Y_NODE DT_NODELABEL(led_y)
#define LED_W_NODE DT_NODELABEL(led_w)
#define RGB_R_NODE DT_NODELABEL(rgb_r)
#define RGB_G_NODE DT_NODELABEL(rgb_g)
#define RGB_B_NODE DT_NODELABEL(rgb_b)

struct tb03f_led {
	const char *name;
	struct gpio_dt_spec gpio;
	uint32_t marker;
};

static const struct tb03f_led leds[] = {
	{
		.name = "yellow",
		.gpio = GPIO_DT_SPEC_GET(LED_Y_NODE, gpios),
		.marker = 0x8258b401u,
	},
	{
		.name = "white",
		.gpio = GPIO_DT_SPEC_GET(LED_W_NODE, gpios),
		.marker = 0x8258b402u,
	},
	{
		.name = "red",
		.gpio = GPIO_DT_SPEC_GET(RGB_R_NODE, gpios),
		.marker = 0x8258b403u,
	},
	{
		.name = "green",
		.gpio = GPIO_DT_SPEC_GET(RGB_G_NODE, gpios),
		.marker = 0x8258b404u,
	},
	{
		.name = "blue",
		.gpio = GPIO_DT_SPEC_GET(RGB_B_NODE, gpios),
		.marker = 0x8258b405u,
	},
};

volatile uint32_t tlsr_tb03f_led_ring_marker;
volatile uint32_t tlsr_tb03f_led_ring_step;
volatile int32_t tlsr_tb03f_led_ring_last_ret;

static FUNC_NORETURN void park(uint32_t marker)
{
	tlsr_tb03f_led_ring_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

static void led_off_all(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(leds); ++i) {
		int ret = gpio_pin_set_dt(&leds[i].gpio, 0);

		tlsr_tb03f_led_ring_last_ret = ret;
		if (ret != 0) {
			park(0x8258eb10u + (uint32_t)i);
		}
	}
}

int main(void)
{
	tlsr_tb03f_led_ring_marker = 0x8258b400u;

	for (size_t i = 0; i < ARRAY_SIZE(leds); ++i) {
		int ret;

		if (!gpio_is_ready_dt(&leds[i].gpio)) {
			park(0x8258eb01u + (uint32_t)i);
		}

		ret = gpio_pin_configure_dt(&leds[i].gpio, GPIO_OUTPUT_INACTIVE);
		tlsr_tb03f_led_ring_last_ret = ret;
		if (ret != 0) {
			park(0x8258eb06u + (uint32_t)i);
		}
	}

	printk("tlsr8258 tb03f led ring start\n");
	led_off_all();

	for (;;) {
		for (size_t i = 0; i < ARRAY_SIZE(leds); ++i) {
			int ret;

			led_off_all();

			ret = gpio_pin_set_dt(&leds[i].gpio, 1);
			tlsr_tb03f_led_ring_last_ret = ret;
			if (ret != 0) {
				park(0x8258eb20u + (uint32_t)i);
			}

			tlsr_tb03f_led_ring_step++;
			tlsr_tb03f_led_ring_marker = leds[i].marker;

			printk("led[%u]=%s\n", (unsigned int)i, leds[i].name);
			k_sleep(K_SECONDS(1));
		}
	}
}
