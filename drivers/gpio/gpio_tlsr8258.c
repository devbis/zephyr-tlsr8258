/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_gpio

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <tlsr825x/irq.h>

#define TLSR8258_GPIO_MAX_PIN 7u
#define TLSR8258_GPIO_CORE_INTERRUPT_EN BIT(3)

#define TLSR8258_GPIO_WAKEUP_IRQ ((volatile uint8_t *)0x008005b5u)

struct tlsr8258_gpio_regs {
	uint8_t input;
	uint8_t ie;
	uint8_t oen;
	uint8_t output;
	uint8_t polarity;
	uint8_t ds;
	uint8_t func;
	uint8_t irq_en;
};

struct tlsr8258_gpio_config {
	struct gpio_driver_config common;
	uintptr_t base;
};

struct tlsr8258_gpio_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
};

static volatile struct tlsr8258_gpio_regs *tlsr8258_gpio_regs(const struct device *dev)
{
	const struct tlsr8258_gpio_config *config = dev->config;

	return (volatile struct tlsr8258_gpio_regs *)config->base;
}

static bool tlsr8258_gpio_pin_valid(gpio_pin_t pin)
{
	return pin <= TLSR8258_GPIO_MAX_PIN;
}

static void tlsr8258_gpio_clear_source(void)
{
	*TLSR8258_REG_IRQ_SRC = BIT(TLSR8258_IRQ_GPIO);
}

static void tlsr8258_gpio_dispatch(const struct device *dev)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);
	struct tlsr8258_gpio_data *data = dev->data;
	gpio_port_pins_t pins = regs->irq_en;

	if (pins != 0u) {
		gpio_fire_callbacks(&data->callbacks, dev, pins);
	}
}

static void tlsr8258_gpio_irq_handler(const void *unused)
{
	ARG_UNUSED(unused);

	tlsr8258_gpio_clear_source();

#define TLSR8258_GPIO_DISPATCH(n) tlsr8258_gpio_dispatch(DEVICE_DT_INST_GET(n));
	DT_INST_FOREACH_STATUS_OKAY(TLSR8258_GPIO_DISPATCH)
#undef TLSR8258_GPIO_DISPATCH
}

static void tlsr8258_gpio_irq_connect(void)
{
	IRQ_CONNECT(TLSR8258_IRQ_GPIO, 0, tlsr8258_gpio_irq_handler, NULL, 0);
}

static int tlsr8258_gpio_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	tlsr8258_gpio_irq_connect();
	return 0;
}

static int tlsr8258_gpio_pin_configure(const struct device *dev, gpio_pin_t pin,
				       gpio_flags_t flags)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);
	uint8_t mask;

	if (!tlsr8258_gpio_pin_valid(pin)) {
		return -EINVAL;
	}

	if ((flags & GPIO_SINGLE_ENDED) != 0u) {
		return -ENOTSUP;
	}

	mask = (uint8_t)BIT(pin);

	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0u) {
		regs->output |= mask;
	} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0u) {
		regs->output &= (uint8_t)~mask;
	}

	regs->func |= mask;

	if ((flags & GPIO_OUTPUT) != 0u) {
		regs->oen &= (uint8_t)~mask;
	} else {
		regs->oen |= mask;
	}

	if ((flags & GPIO_INPUT) != 0u) {
		regs->ie |= mask;
	} else {
		regs->ie &= (uint8_t)~mask;
	}

	return 0;
}

static int tlsr8258_gpio_port_get_raw(const struct device *dev,
				      gpio_port_value_t *value)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	*value = regs->input;
	return 0;
}

static int tlsr8258_gpio_port_set_masked_raw(const struct device *dev,
					     gpio_port_pins_t mask,
					     gpio_port_value_t value)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output = (regs->output & (uint8_t)~mask) | (value & mask);
	return 0;
}

static int tlsr8258_gpio_port_set_bits_raw(const struct device *dev,
					   gpio_port_pins_t pins)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output |= pins;
	return 0;
}

static int tlsr8258_gpio_port_clear_bits_raw(const struct device *dev,
					     gpio_port_pins_t pins)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output &= (uint8_t)~pins;
	return 0;
}

static int tlsr8258_gpio_port_toggle_bits(const struct device *dev,
					  gpio_port_pins_t pins)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);

	regs->output ^= pins;
	return 0;
}

static int tlsr8258_gpio_pin_interrupt_configure(const struct device *dev,
						 gpio_pin_t pin,
						 enum gpio_int_mode mode,
						 enum gpio_int_trig trig)
{
	volatile struct tlsr8258_gpio_regs *regs = tlsr8258_gpio_regs(dev);
	unsigned int key;
	uint8_t mask;

	if (!tlsr8258_gpio_pin_valid(pin)) {
		return -EINVAL;
	}

	if (mode == GPIO_INT_MODE_LEVEL || trig == GPIO_INT_TRIG_BOTH) {
		return -ENOTSUP;
	}

	mask = (uint8_t)BIT(pin);
	key = irq_lock();

	regs->irq_en &= (uint8_t)~mask;

	if (mode == GPIO_INT_MODE_DISABLED) {
		tlsr8258_gpio_clear_source();
		irq_unlock(key);
		return 0;
	}

	if (trig == GPIO_INT_TRIG_HIGH) {
		regs->polarity &= (uint8_t)~mask;
	} else if (trig == GPIO_INT_TRIG_LOW) {
		regs->polarity |= mask;
	} else {
		irq_unlock(key);
		return -ENOTSUP;
	}

	*TLSR8258_GPIO_WAKEUP_IRQ |= TLSR8258_GPIO_CORE_INTERRUPT_EN;

	/* Vendor sequence: set polarity, clear source, enable mask, then enable pin. */
	tlsr8258_gpio_clear_source();
	irq_enable(TLSR8258_IRQ_GPIO);
	regs->irq_en |= mask;

	irq_unlock(key);
	return 0;
}

static int tlsr8258_gpio_manage_callback(const struct device *dev,
					 struct gpio_callback *callback,
					 bool set)
{
	struct tlsr8258_gpio_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static DEVICE_API(gpio, tlsr8258_gpio_api) = {
	.pin_configure = tlsr8258_gpio_pin_configure,
	.port_get_raw = tlsr8258_gpio_port_get_raw,
	.port_set_masked_raw = tlsr8258_gpio_port_set_masked_raw,
	.port_set_bits_raw = tlsr8258_gpio_port_set_bits_raw,
	.port_clear_bits_raw = tlsr8258_gpio_port_clear_bits_raw,
	.port_toggle_bits = tlsr8258_gpio_port_toggle_bits,
	.pin_interrupt_configure = tlsr8258_gpio_pin_interrupt_configure,
	.manage_callback = tlsr8258_gpio_manage_callback,
};

#define TLSR8258_GPIO_INIT(n)							\
	static const struct tlsr8258_gpio_config tlsr8258_gpio_config_##n = {	\
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(n),			\
		.base = DT_INST_REG_ADDR(n),					\
	};									\
	static struct tlsr8258_gpio_data tlsr8258_gpio_data_##n;		\
										\
	DEVICE_DT_INST_DEFINE(n, tlsr8258_gpio_init, NULL,			\
			      &tlsr8258_gpio_data_##n,			\
			      &tlsr8258_gpio_config_##n,			\
			      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,		\
			      &tlsr8258_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_GPIO_INIT)
