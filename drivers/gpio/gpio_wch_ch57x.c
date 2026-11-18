/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT wch_ch57x_gpio

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>

#define PORT_DIR    0x00U
#define PORT_PIN    0x04U
#define PORT_OUT    0x08U
#define PORT_CLR    0x0cU
#define PORT_PU     0x10U
#define PORT_PD_DRV 0x14U

struct gpio_ch57x_config {
	struct gpio_driver_config common;
	uintptr_t base;
};

struct gpio_ch57x_data {
	struct gpio_driver_data common;
};

static void gpio_ch57x_rmw(uintptr_t addr, uint32_t mask, bool set)
{
	unsigned int key = irq_lock();
	uint32_t val = sys_read32(addr);

	sys_write32(set ? (val | mask) : (val & ~mask), addr);
	irq_unlock(key);
}

static int gpio_ch57x_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_ch57x_config *cfg = dev->config;
	uint32_t mask = BIT(pin);

	if ((flags & (GPIO_OPEN_DRAIN | GPIO_OPEN_SOURCE)) != 0U) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_OUTPUT) != 0U) {
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			gpio_ch57x_rmw(cfg->base + PORT_OUT, mask, true);
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			sys_write32(mask, cfg->base + PORT_CLR);
		}
		/* 5 mA push-pull output */
		gpio_ch57x_rmw(cfg->base + PORT_PD_DRV, mask, false);
		gpio_ch57x_rmw(cfg->base + PORT_PU, mask, false);
		gpio_ch57x_rmw(cfg->base + PORT_DIR, mask, true);
	} else if ((flags & GPIO_INPUT) != 0U) {
		gpio_ch57x_rmw(cfg->base + PORT_DIR, mask, false);
		gpio_ch57x_rmw(cfg->base + PORT_PU, mask, (flags & GPIO_PULL_UP) != 0U);
		gpio_ch57x_rmw(cfg->base + PORT_PD_DRV, mask, (flags & GPIO_PULL_DOWN) != 0U);
	} else {
		/* Disconnected: floating input */
		gpio_ch57x_rmw(cfg->base + PORT_DIR, mask, false);
		gpio_ch57x_rmw(cfg->base + PORT_PU, mask, false);
		gpio_ch57x_rmw(cfg->base + PORT_PD_DRV, mask, false);
	}

	return 0;
}

static int gpio_ch57x_port_get_raw(const struct device *dev, uint32_t *value)
{
	const struct gpio_ch57x_config *cfg = dev->config;

	*value = sys_read32(cfg->base + PORT_PIN);

	return 0;
}

static int gpio_ch57x_port_set_masked_raw(const struct device *dev, uint32_t mask, uint32_t value)
{
	const struct gpio_ch57x_config *cfg = dev->config;
	unsigned int key = irq_lock();
	uint32_t out = sys_read32(cfg->base + PORT_OUT);

	sys_write32((out & ~mask) | (value & mask), cfg->base + PORT_OUT);
	irq_unlock(key);

	return 0;
}

static int gpio_ch57x_port_set_bits_raw(const struct device *dev, uint32_t mask)
{
	const struct gpio_ch57x_config *cfg = dev->config;

	gpio_ch57x_rmw(cfg->base + PORT_OUT, mask, true);

	return 0;
}

static int gpio_ch57x_port_clear_bits_raw(const struct device *dev, uint32_t mask)
{
	const struct gpio_ch57x_config *cfg = dev->config;

	sys_write32(mask, cfg->base + PORT_CLR);

	return 0;
}

static int gpio_ch57x_port_toggle_bits(const struct device *dev, uint32_t mask)
{
	const struct gpio_ch57x_config *cfg = dev->config;
	unsigned int key = irq_lock();

	sys_write32(sys_read32(cfg->base + PORT_OUT) ^ mask, cfg->base + PORT_OUT);
	irq_unlock(key);

	return 0;
}

static DEVICE_API(gpio, gpio_ch57x_api) = {
	.pin_configure = gpio_ch57x_configure,
	.port_get_raw = gpio_ch57x_port_get_raw,
	.port_set_masked_raw = gpio_ch57x_port_set_masked_raw,
	.port_set_bits_raw = gpio_ch57x_port_set_bits_raw,
	.port_clear_bits_raw = gpio_ch57x_port_clear_bits_raw,
	.port_toggle_bits = gpio_ch57x_port_toggle_bits,
};

#define GPIO_CH57X_INIT(n)                                                                         \
	static const struct gpio_ch57x_config gpio_ch57x_cfg_##n = {                               \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n)},                   \
		.base = DT_INST_REG_ADDR(n),                                                       \
	};                                                                                         \
	static struct gpio_ch57x_data gpio_ch57x_data_##n;                                         \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, &gpio_ch57x_data_##n, &gpio_ch57x_cfg_##n,            \
			      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY, &gpio_ch57x_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_CH57X_INIT)
