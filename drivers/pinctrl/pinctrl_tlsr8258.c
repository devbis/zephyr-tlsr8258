/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#define TLSR8258_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR8258_REG_GPIO_IE(pin)   TLSR8258_REG8(0x00800581u + (((pin) >> 8) << 3))
#define TLSR8258_REG_GPIO_OEN(pin)  TLSR8258_REG8(0x00800582u + (((pin) >> 8) << 3))
#define TLSR8258_REG_GPIO_FUNC(pin) TLSR8258_REG8(0x00800586u + (((pin) >> 8) << 3))
#define TLSR8258_REG_MUX(pin)       TLSR8258_REG8(0x008005a8u + (((pin) >> 8) << 1) + \
						   (((pin) & 0xf0u) ? 1u : 0u))

#define TLSR8258_REG_ANA_ADDR TLSR8258_REG8(0x008000b8u)
#define TLSR8258_REG_ANA_DATA TLSR8258_REG8(0x008000b9u)
#define TLSR8258_REG_ANA_CTRL TLSR8258_REG8(0x008000bau)

#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

#define TLSR8258_GPIO_GROUP_B      0x100u
#define TLSR8258_GPIO_GROUP_C      0x200u
#define TLSR8258_GPIO_ANALOG_PB_IE 0xbdu
#define TLSR8258_GPIO_ANALOG_PC_IE 0xc0u

static void tlsr8258_analog_wait(void)
{
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr8258_analog_read(uint8_t addr)
{
	unsigned int key = irq_lock();
	uint8_t data;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	tlsr8258_analog_wait();
	data = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);

	return data;
}

static void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	tlsr8258_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);
}

static void tlsr8258_gpio_pull(uint32_t pin, uint8_t pull)
{
	uint8_t reg = 0x0eu + ((pin >> 8) << 1) + (((pin & 0xf0u) != 0u) ? 1u : 0u);
	uint8_t shift;
	uint8_t mask;

	if ((pin & 0x11u) != 0u) {
		shift = 0u;
		mask = 0xfcu;
	} else if ((pin & 0x22u) != 0u) {
		shift = 2u;
		mask = 0xf3u;
	} else if ((pin & 0x44u) != 0u) {
		shift = 4u;
		mask = 0xcfu;
	} else if ((pin & 0x88u) != 0u) {
		shift = 6u;
		mask = 0x3fu;
	} else {
		return;
	}

	tlsr8258_analog_write(reg, (tlsr8258_analog_read(reg) & mask) | (pull << shift));
}

static int tlsr8258_gpio_uart_mux(uint32_t pin, uint8_t *mask, uint8_t *value)
{
	switch (pin) {
	case TLSR8258_GPIO_PA0:
		/* UART_RX: 0x5a8[1:0] = 2 */
		*mask = (uint8_t)~GENMASK(1, 0);
		*value = BIT(1);
		break;
	case TLSR8258_GPIO_PA2:
		/* UART_TX: 0x5a8[5:4] = 1 */
		*mask = (uint8_t)~GENMASK(5, 4);
		*value = BIT(4);
		break;
	case TLSR8258_GPIO_PB0:
		/* UART_RX: 0x5aa[1:0] = 1 */
		*mask = (uint8_t)~GENMASK(1, 0);
		*value = BIT(0);
		break;
	case TLSR8258_GPIO_PB1:
		/* UART_TX: 0x5aa[3:2] = 1 */
		*mask = (uint8_t)~GENMASK(3, 2);
		*value = BIT(2);
		break;
	case TLSR8258_GPIO_PB7:
		/* UART_RX: 0x5ab[7:6] = 2 */
		*mask = (uint8_t)~GENMASK(7, 6);
		*value = BIT(7);
		break;
	case TLSR8258_GPIO_PC2:
		/* 7816_TRX/UART_TX: 0x5ac[5:4] = 1 */
		*mask = (uint8_t)~GENMASK(5, 4);
		*value = BIT(4);
		break;
	case TLSR8258_GPIO_PC3:
		/* UART_RX: 0x5ac[7:6] = 1 */
		*mask = (uint8_t)~GENMASK(7, 6);
		*value = BIT(6);
		break;
	case TLSR8258_GPIO_PC5:
		/* UART_RX: 0x5ad[3:2] = 1 */
		*mask = (uint8_t)~GENMASK(3, 2);
		*value = BIT(2);
		break;
	case TLSR8258_GPIO_PD0:
		/* 7816_TRX/UART_TX: 0x5ae[1:0] = 2 */
		*mask = (uint8_t)~GENMASK(1, 0);
		*value = BIT(1);
		break;
	case TLSR8258_GPIO_PD3:
		/* UART_TX7816: 0x5ae[7:6] = 2 */
		*mask = (uint8_t)~GENMASK(7, 6);
		*value = BIT(7);
		break;
	case TLSR8258_GPIO_PD6:
		/* UART_RX: 0x5af[5:4] = 1 */
		*mask = (uint8_t)~GENMASK(5, 4);
		*value = BIT(4);
		break;
	case TLSR8258_GPIO_PD7:
		/* 7816_TRX/UART_TX: 0x5af[7:6] = 2 */
		*mask = (uint8_t)~GENMASK(7, 6);
		*value = BIT(7);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

/*
 * PWM pin multiplexing. Each entry selects option "sel" of the 2-bit field at
 * "shift" in the mux register of the pin (TLSR8258_REG_MUX). Options follow the
 * vendor gpio_set_mux() table; the register address is derived from the pin.
 */
struct tlsr8258_pwm_mux {
	uint32_t pin;
	uint32_t func;
	uint8_t shift;
	uint8_t sel;
};

static const struct tlsr8258_pwm_mux tlsr8258_pwm_mux_table[] = {
	{TLSR8258_GPIO_PA0, TLSR8258_FUNC_PWM0_N, 0, 1},
	{TLSR8258_GPIO_PA2, TLSR8258_FUNC_PWM0, 4, 2},
	{TLSR8258_GPIO_PA3, TLSR8258_FUNC_PWM1, 6, 2},
	{TLSR8258_GPIO_PA4, TLSR8258_FUNC_PWM2, 0, 2},
	{TLSR8258_GPIO_PB0, TLSR8258_FUNC_PWM3, 0, 0},
	{TLSR8258_GPIO_PB1, TLSR8258_FUNC_PWM4, 2, 0},
	{TLSR8258_GPIO_PB2, TLSR8258_FUNC_PWM5, 4, 0},
	{TLSR8258_GPIO_PB3, TLSR8258_FUNC_PWM0_N, 6, 0},
	{TLSR8258_GPIO_PB4, TLSR8258_FUNC_PWM4, 0, 1},
	{TLSR8258_GPIO_PB5, TLSR8258_FUNC_PWM5, 2, 1},
	{TLSR8258_GPIO_PC0, TLSR8258_FUNC_PWM4_N, 0, 1},
	{TLSR8258_GPIO_PC1, TLSR8258_FUNC_PWM1_N, 2, 1},
	{TLSR8258_GPIO_PC1, TLSR8258_FUNC_PWM0, 2, 2},
	{TLSR8258_GPIO_PC2, TLSR8258_FUNC_PWM0, 4, 0},
	{TLSR8258_GPIO_PC3, TLSR8258_FUNC_PWM1, 6, 0},
	{TLSR8258_GPIO_PC4, TLSR8258_FUNC_PWM2, 0, 0},
	{TLSR8258_GPIO_PC4, TLSR8258_FUNC_PWM0_N, 0, 2},
	{TLSR8258_GPIO_PC5, TLSR8258_FUNC_PWM3_N, 2, 0},
	{TLSR8258_GPIO_PC6, TLSR8258_FUNC_PWM4_N, 4, 2},
	{TLSR8258_GPIO_PC7, TLSR8258_FUNC_PWM5_N, 6, 2},
	{TLSR8258_GPIO_PD2, TLSR8258_FUNC_PWM3, 4, 2},
	{TLSR8258_GPIO_PD3, TLSR8258_FUNC_PWM1_N, 6, 0},
	{TLSR8258_GPIO_PD4, TLSR8258_FUNC_PWM2_N, 0, 2},
	{TLSR8258_GPIO_PD5, TLSR8258_FUNC_PWM0, 2, 0},
	{TLSR8258_GPIO_PD5, TLSR8258_FUNC_PWM0_N, 2, 2},
};

static int tlsr8258_gpio_pwm_set_func(uint32_t pin, uint32_t func)
{
	uint8_t bit = pin & 0xffu;

	for (unsigned int i = 0u; i < ARRAY_SIZE(tlsr8258_pwm_mux_table); i++) {
		const struct tlsr8258_pwm_mux *entry = &tlsr8258_pwm_mux_table[i];
		uint8_t field = (uint8_t)(GENMASK(1, 0) << entry->shift);

		if (entry->pin != pin || entry->func != func) {
			continue;
		}

		TLSR8258_REG_MUX(pin) = (TLSR8258_REG_MUX(pin) & (uint8_t)~field) |
					(uint8_t)(entry->sel << entry->shift);
		TLSR8258_REG_GPIO_FUNC(pin) &= (uint8_t)~bit;
		return 0;
	}

	return -ENOTSUP;
}

/*
 * I2C pin mux, vendor gpio_set_mux() AS_I2C values. SDA and SCL of each
 * supported pair are listed here; PA3/PA4 and PB6/PD7 also need 0x5b7 set.
 */
struct tlsr8258_i2c_mux {
	uint32_t pin;
	uint8_t shift;
	uint8_t sel;
};

static const struct tlsr8258_i2c_mux tlsr8258_i2c_mux_table[] = {
	{TLSR8258_GPIO_PA3, 6, 0},
	{TLSR8258_GPIO_PA4, 0, 0},
	{TLSR8258_GPIO_PB6, 4, 1},
	{TLSR8258_GPIO_PC0, 0, 0},
	{TLSR8258_GPIO_PC1, 2, 0},
	{TLSR8258_GPIO_PC2, 4, 2},
	{TLSR8258_GPIO_PC3, 6, 2},
	{TLSR8258_GPIO_PD7, 6, 0},
};

static void tlsr8258_gpio_i2c_pin_enable(uint32_t pin)
{
	uint8_t i2c_en;
	uint8_t spi_en;

	switch (pin) {
	case TLSR8258_GPIO_PA3:
		i2c_en = BIT(4);
		spi_en = BIT(0);
		break;
	case TLSR8258_GPIO_PA4:
		i2c_en = BIT(5);
		spi_en = BIT(1);
		break;
	case TLSR8258_GPIO_PB6:
		i2c_en = BIT(6);
		spi_en = BIT(2);
		break;
	case TLSR8258_GPIO_PD7:
		i2c_en = BIT(7);
		spi_en = BIT(3);
		break;
	default:
		/* PC0-PC3 have no 0x5b7 enable bit. */
		return;
	}

	/* 0x5b7: [4..7] I2C input enable, [0..3] SPI input enable. */
	TLSR8258_REG8(0x008005b7u) = (uint8_t)((TLSR8258_REG8(0x008005b7u) | i2c_en) &
						(uint8_t)~spi_en);
}

static int tlsr8258_gpio_i2c_set_func(uint32_t pin)
{
	for (unsigned int i = 0u; i < ARRAY_SIZE(tlsr8258_i2c_mux_table); i++) {
		const struct tlsr8258_i2c_mux *entry = &tlsr8258_i2c_mux_table[i];
		uint8_t field = (uint8_t)(GENMASK(1, 0) << entry->shift);

		if (entry->pin != pin) {
			continue;
		}

		TLSR8258_REG_MUX(pin) = (TLSR8258_REG_MUX(pin) & (uint8_t)~field) |
					(uint8_t)(entry->sel << entry->shift);
		tlsr8258_gpio_i2c_pin_enable(pin);
		return 0;
	}

	return -ENOTSUP;
}

/*
 * SPI master pin mux, vendor gpio_set_func(AS_SPI) and spi_master_gpio_set()
 * values. Only SDO, SDI and SCK are listed; CS is a plain GPIO that the SPI
 * driver drives through cs-gpios.
 */
struct tlsr8258_spi_mux {
	uint32_t pin;
	uint8_t shift;
	uint8_t sel;
	uint8_t in_en;
};

static const struct tlsr8258_spi_mux tlsr8258_spi_mux_table[] = {
	{TLSR8258_GPIO_PA2, 4, 0, 0},
	{TLSR8258_GPIO_PA3, 6, 0, BIT(0)},
	{TLSR8258_GPIO_PA4, 0, 0, BIT(1)},
	{TLSR8258_GPIO_PB6, 4, 1, BIT(2)},
	{TLSR8258_GPIO_PB7, 6, 1, 0},
	{TLSR8258_GPIO_PD7, 6, 0, BIT(3)},
};

static int tlsr8258_gpio_spi_set_func(uint32_t pin)
{
	for (unsigned int i = 0u; i < ARRAY_SIZE(tlsr8258_spi_mux_table); i++) {
		const struct tlsr8258_spi_mux *entry = &tlsr8258_spi_mux_table[i];
		uint8_t field = (uint8_t)(GENMASK(1, 0) << entry->shift);

		if (entry->pin != pin) {
			continue;
		}

		TLSR8258_REG_MUX(pin) = (TLSR8258_REG_MUX(pin) & (uint8_t)~field) |
					(uint8_t)(entry->sel << entry->shift);

		/* 0x5b6: PA group clears bits 4 and 6, PB/PD group clears bit 6. */
		TLSR8258_REG8(0x008005b6u) &=
			(uint8_t)~(((pin & 0xf00u) == 0u) ? 0x50u : 0x40u);

		/* 0x5b7: SPI input enable per pin, the I2C bit for the pin is cleared. */
		if (entry->in_en != 0u) {
			TLSR8258_REG8(0x008005b7u) =
				(uint8_t)((TLSR8258_REG8(0x008005b7u) | entry->in_en) &
					  (uint8_t)~(entry->in_en << 4));
		}
		return 0;
	}

	return -ENOTSUP;
}

static int tlsr8258_gpio_set_func(uint32_t pin, uint32_t func)
{
	uint8_t bit = pin & 0xffu;

	if (func == TLSR8258_FUNC_GPIO) {
		TLSR8258_REG_GPIO_FUNC(pin) |= bit;
		return 0;
	}

	if (func >= TLSR8258_FUNC_PWM0 && func <= TLSR8258_FUNC_PWM5_N) {
		return tlsr8258_gpio_pwm_set_func(pin, func);
	}

	if (func == TLSR8258_FUNC_UART) {
		uint8_t mask;
		uint8_t value;
		int ret = tlsr8258_gpio_uart_mux(pin, &mask, &value);

		if (ret < 0) {
			return ret;
		}

		TLSR8258_REG_MUX(pin) = (TLSR8258_REG_MUX(pin) & mask) | value;
		TLSR8258_REG_GPIO_FUNC(pin) &= (uint8_t)~bit;
		return 0;
	}

	if (func == TLSR8258_FUNC_I2C) {
		int ret = tlsr8258_gpio_i2c_set_func(pin);

		if (ret < 0) {
			return ret;
		}

		TLSR8258_REG_GPIO_FUNC(pin) &= (uint8_t)~bit;
		return 0;
	}

	if (func == TLSR8258_FUNC_SPI) {
		int ret = tlsr8258_gpio_spi_set_func(pin);

		if (ret < 0) {
			return ret;
		}

		TLSR8258_REG_GPIO_FUNC(pin) &= (uint8_t)~bit;
		return 0;
	}

	return -ENOTSUP;
}

static void tlsr8258_gpio_input_enable(uint32_t pin, bool enable)
{
	uint8_t bit = pin & 0xffu;
	uint32_t group = pin & 0xf00u;
	uint8_t analog_reg;

	if (group == TLSR8258_GPIO_GROUP_B) {
		analog_reg = TLSR8258_GPIO_ANALOG_PB_IE;
	} else if (group == TLSR8258_GPIO_GROUP_C) {
		analog_reg = TLSR8258_GPIO_ANALOG_PC_IE;
	} else {
		if (enable) {
			TLSR8258_REG_GPIO_IE(pin) |= bit;
		} else {
			TLSR8258_REG_GPIO_IE(pin) &= (uint8_t)~bit;
		}
		return;
	}

	if (enable) {
		tlsr8258_analog_write(analog_reg, tlsr8258_analog_read(analog_reg) | bit);
	} else {
		tlsr8258_analog_write(analog_reg, tlsr8258_analog_read(analog_reg) & (uint8_t)~bit);
	}
}

static void tlsr8258_gpio_output_enable(uint32_t pin, bool enable)
{
	uint8_t bit = pin & 0xffu;

	if (enable) {
		TLSR8258_REG_GPIO_OEN(pin) &= (uint8_t)~bit;
	} else {
		TLSR8258_REG_GPIO_OEN(pin) |= bit;
	}
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	for (uint8_t i = 0u; i < pin_cnt; i++) {
		uint32_t pin = TLSR8258_PINMUX_PIN(pins[i].pinmux);
		uint32_t func = TLSR8258_PINMUX_FUNC(pins[i].pinmux);
		int ret;

		tlsr8258_gpio_input_enable(pin, pins[i].input_enable);
		tlsr8258_gpio_output_enable(pin, pins[i].output_enable);
		tlsr8258_gpio_pull(pin, pins[i].pull);

		ret = tlsr8258_gpio_set_func(pin, func);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}
