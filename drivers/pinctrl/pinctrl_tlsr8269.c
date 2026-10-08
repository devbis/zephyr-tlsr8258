/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/util.h>

#define TLSR8269_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR8269_REG_GPIO_IE(pin)   TLSR8269_REG8(0x00800581u + (((pin) >> 8) << 3))
#define TLSR8269_REG_GPIO_OEN(pin)  TLSR8269_REG8(0x00800582u + (((pin) >> 8) << 3))
#define TLSR8269_REG_GPIO_FUNC(pin) TLSR8269_REG8(0x00800586u + (((pin) >> 8) << 3))

/* UART pin select, one register per port: 0x5b0 (A), 0x5b1 (B), 0x5b2 (C). */
#define TLSR8269_REG_UART_PIN_SEL(pin) TLSR8269_REG8(0x008005b0u + ((pin) >> 8))

/*
 * Pull-up/pull-down is not configured yet. The 826x analog pull registers
 * are not mapped in this port, so requests for a pull are refused rather
 * than written to an unverified address.
 */

static int tlsr8269_gpio_set_func(uint32_t pin, uint32_t func)
{
	uint8_t bit = pin & 0xffu;

	if (func == TLSR8269_FUNC_GPIO) {
		TLSR8269_REG_GPIO_FUNC(pin) |= bit;
		return 0;
	}

	if (func == TLSR8269_FUNC_UART) {
		uint8_t sel;

		/*
		 * Vendor uart.h: PA6/PA7 share 0x5b0[7]; PB2/PB3 use 0x5b1[3:2];
		 * PC2/PC3 use 0x5b2[3:2].
		 */
		switch (pin) {
		case TLSR8269_GPIO_PA6:
		case TLSR8269_GPIO_PA7:
			sel = BIT(7);
			break;
		case TLSR8269_GPIO_PB2:
		case TLSR8269_GPIO_PB3:
		case TLSR8269_GPIO_PC2:
		case TLSR8269_GPIO_PC3:
			sel = bit;
			break;
		default:
			return -ENOTSUP;
		}

		TLSR8269_REG_UART_PIN_SEL(pin) |= sel;
		TLSR8269_REG_GPIO_FUNC(pin) &= (uint8_t)~bit;
		return 0;
	}

	return -ENOTSUP;
}

static void tlsr8269_gpio_input_enable(uint32_t pin, bool enable)
{
	uint8_t bit = pin & 0xffu;

	if (enable) {
		TLSR8269_REG_GPIO_IE(pin) |= bit;
	} else {
		TLSR8269_REG_GPIO_IE(pin) &= (uint8_t)~bit;
	}
}

static void tlsr8269_gpio_output_enable(uint32_t pin, bool enable)
{
	uint8_t bit = pin & 0xffu;

	if (enable) {
		TLSR8269_REG_GPIO_OEN(pin) &= (uint8_t)~bit;
	} else {
		TLSR8269_REG_GPIO_OEN(pin) |= bit;
	}
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	for (uint8_t i = 0u; i < pin_cnt; i++) {
		if (pins[i].pull != TLSR8269_PIN_PULL_FLOAT) {
			return -ENOTSUP;
		}
	}

	for (uint8_t i = 0u; i < pin_cnt; i++) {
		uint32_t pin = TLSR8269_PINMUX_PIN(pins[i].pinmux);
		uint32_t func = TLSR8269_PINMUX_FUNC(pins[i].pinmux);
		int ret;

		tlsr8269_gpio_input_enable(pin, pins[i].input_enable);
		tlsr8269_gpio_output_enable(pin, pins[i].output_enable);

		ret = tlsr8269_gpio_set_func(pin, func);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}
