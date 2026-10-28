/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT phyplus_phy6252_gpio

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define GPIO_BASE DT_INST_REG_ADDR(0)
#define GPIO_DATA (GPIO_BASE + 0x00U)
#define GPIO_DIR (GPIO_BASE + 0x04U)
#define GPIO_INT_EN (GPIO_BASE + 0x30U)
#define GPIO_INT_MASK_REG (GPIO_BASE + 0x34U)
#define GPIO_INT_TYPE (GPIO_BASE + 0x38U)
#define GPIO_INT_POLARITY (GPIO_BASE + 0x3cU)
#define GPIO_INT_STATUS (GPIO_BASE + 0x40U)
#define GPIO_INT_EOI (GPIO_BASE + 0x4cU)
#define GPIO_INPUT_DATA (GPIO_BASE + 0x50U)
#define MUX_ANALOG 0x40003800U
#define MUX_ENABLE 0x4000380cU
#define MUX_PAD_ENABLE 0x40003814U
#define AON_PULL_BASE 0x4000f008U

struct phy6252_gpio_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
};

static const struct gpio_driver_config gpio_config = {
	.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(0),
};

static struct phy6252_gpio_data gpio_data;

/* Each pad's pull field is two bits in one of four AON registers. */
static const uint8_t pull_reg[] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
				   1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3};
static const uint8_t pull_shift[] = {1, 4, 7, 10, 22, 28, 1, 4, 13, 16, 19, 22,
				     25, 1, 10, 13, 16, 19, 22, 4, 7, 10, 13};

static void update_bits(uintptr_t reg, uint32_t mask, uint32_t value)
{
	sys_write32((sys_read32(reg) & ~mask) | (value & mask), reg);
}

static int phy6252_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_driver_config *config = dev->config;
	uint32_t pull = 0U;
	unsigned int key;

	if (!gpio_port_pin_is_supported(config->port_pin_mask, pin)) {
		return -EINVAL;
	}
	if ((flags & GPIO_SINGLE_ENDED) != 0U ||
	    (flags & (GPIO_INPUT | GPIO_OUTPUT)) == (GPIO_INPUT | GPIO_OUTPUT) ||
	    (flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) == (GPIO_PULL_UP | GPIO_PULL_DOWN)) {
		return -ENOTSUP;
	}
	if ((flags & GPIO_PULL_UP) != 0U) {
		pull = 2U;
	} else if ((flags & GPIO_PULL_DOWN) != 0U) {
		pull = 3U;
	}

	key = irq_lock();
	update_bits(MUX_ENABLE, BIT(pin), 0U);
	if (pin == 2U || pin == 3U) {
		update_bits(MUX_PAD_ENABLE, BIT(pin - 2U), BIT(pin - 2U));
	}
	if (pin >= 7U && pin <= 16U) {
		update_bits(MUX_ANALOG, BIT(pin - 7U), 0U);
	}
	update_bits(AON_PULL_BASE + 4U * pull_reg[pin], 3U << pull_shift[pin],
		    pull << pull_shift[pin]);
	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
		update_bits(GPIO_DATA, BIT(pin), BIT(pin));
	} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
		update_bits(GPIO_DATA, BIT(pin), 0U);
	}
	update_bits(GPIO_DIR, BIT(pin), (flags & GPIO_OUTPUT) != 0U ? BIT(pin) : 0U);
	irq_unlock(key);
	return 0;
}

static int phy6252_get(const struct device *dev, gpio_port_value_t *value)
{
	const struct gpio_driver_config *config = dev->config;

	*value = sys_read32(GPIO_INPUT_DATA) & config->port_pin_mask;
	return 0;
}

static int phy6252_masked(const struct device *dev, gpio_port_pins_t mask,
			  gpio_port_value_t value)
{
	const struct gpio_driver_config *config = dev->config;
	unsigned int key = irq_lock();

	update_bits(GPIO_DATA, mask & config->port_pin_mask, value);
	irq_unlock(key);
	return 0;
}

static int phy6252_set(const struct device *dev, gpio_port_pins_t pins)
{
	return phy6252_masked(dev, pins, pins);
}

static int phy6252_clear(const struct device *dev, gpio_port_pins_t pins)
{
	return phy6252_masked(dev, pins, 0U);
}

static int phy6252_toggle(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_driver_config *config = dev->config;
	unsigned int key = irq_lock();

	sys_write32(sys_read32(GPIO_DATA) ^ (pins & config->port_pin_mask), GPIO_DATA);
	irq_unlock(key);
	return 0;
}

static int phy6252_interrupt(const struct device *dev, gpio_pin_t pin,
			     enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct gpio_driver_config *config = dev->config;
	uint32_t bit;
	unsigned int key;

	if (!gpio_port_pin_is_supported(config->port_pin_mask, pin)) {
		return -EINVAL;
	}
	if (mode != GPIO_INT_MODE_DISABLED && mode != GPIO_INT_MODE_LEVEL &&
	    mode != GPIO_INT_MODE_EDGE) {
		return -ENOTSUP;
	}
	if (mode != GPIO_INT_MODE_DISABLED && trig != GPIO_INT_TRIG_LOW &&
	    trig != GPIO_INT_TRIG_HIGH) {
		return -ENOTSUP;
	}
	bit = BIT(pin);
	key = irq_lock();
	update_bits(GPIO_INT_MASK_REG, bit, bit);
	update_bits(GPIO_INT_EN, bit, 0U);
	sys_write32(bit, GPIO_INT_EOI);
	if (mode != GPIO_INT_MODE_DISABLED) {
		update_bits(GPIO_INT_TYPE, bit, mode == GPIO_INT_MODE_EDGE ? bit : 0U);
		update_bits(GPIO_INT_POLARITY, bit, trig == GPIO_INT_TRIG_HIGH ? bit : 0U);
		update_bits(GPIO_INT_EN, bit, bit);
		update_bits(GPIO_INT_MASK_REG, bit, 0U);
	}
	irq_unlock(key);
	return 0;
}

static int phy6252_callback(const struct device *dev, struct gpio_callback *callback, bool set)
{
	struct phy6252_gpio_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static void phy6252_gpio_isr(const struct device *dev)
{
	struct phy6252_gpio_data *data = dev->data;
	uint32_t pending = sys_read32(GPIO_INT_STATUS);

	sys_write32(pending, GPIO_INT_EOI);
	gpio_fire_callbacks(&data->callbacks, dev, pending);
}

static int phy6252_gpio_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	sys_set_bit(0x40000008U, 13U);
	sys_write32(0U, GPIO_INT_EN);
	sys_write32(UINT32_MAX, GPIO_INT_MASK_REG);
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), phy6252_gpio_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));
	return 0;
}

static DEVICE_API(gpio, phy6252_gpio_api) = {
	.pin_configure = phy6252_pin_configure,
	.port_get_raw = phy6252_get,
	.port_set_masked_raw = phy6252_masked,
	.port_set_bits_raw = phy6252_set,
	.port_clear_bits_raw = phy6252_clear,
	.port_toggle_bits = phy6252_toggle,
	.pin_interrupt_configure = phy6252_interrupt,
	.manage_callback = phy6252_callback,
};

DEVICE_DT_INST_DEFINE(0, phy6252_gpio_init, NULL, &gpio_data, &gpio_config,
		      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY, &phy6252_gpio_api);
