/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* Registers of the IOMUX and of the AON block, as in the PHY62XX SDK 3.1.1 */
#define IOMUX_ANALOG_EN 0x40003800U
#define IOMUX_FULL_MUX_EN 0x4000380cU
#define IOMUX_PAD_EN 0x40003814U
#define IOMUX_GPIO_SEL(pin) (0x40003818U + 4U * ((pin) / 4U))
#define AON_PULL_BASE 0x4000f008U
#define SW_CLK 0x40000008U
#define SW_CLK_IOMUX BIT(7)

#define NUM_PINS 23U

/* Each pad has two bits of pull in one of four registers of the AON block */
static const uint8_t pull_reg[NUM_PINS] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
					   1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3};
static const uint8_t pull_shift[NUM_PINS] = {1, 4, 7, 10, 22, 28, 1, 4, 13, 16, 19, 22,
					     25, 1, 10, 13, 16, 19, 22, 4, 7, 10, 13};

static void update_bits(uintptr_t reg, uint32_t mask, uint32_t value)
{
	sys_write32((sys_read32(reg) & ~mask) | (value & mask), reg);
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		const uint32_t pin = pins[i].pin;
		const uint32_t shift = 8U * (pin % 4U);
		unsigned int key;

		if (pin >= NUM_PINS) {
			return -EINVAL;
		}

		key = irq_lock();
		sys_set_bits(SW_CLK, SW_CLK_IOMUX);
		if (pin == 2U || pin == 3U) {
			/* The pads of the debug interface go to the peripherals */
			update_bits(IOMUX_PAD_EN, BIT(pin - 2U), BIT(pin - 2U));
		}
		if (pin >= 7U && pin <= 16U) {
			update_bits(IOMUX_ANALOG_EN, BIT(pin - 7U), 0U);
		}
		update_bits(AON_PULL_BASE + 4U * pull_reg[pin], 3U << pull_shift[pin],
			    (uint32_t)pins[i].pull << pull_shift[pin]);
		if (pins[i].function == PHY62X2_FMUX_NONE) {
			update_bits(IOMUX_FULL_MUX_EN, BIT(pin), 0U);
		} else {
			update_bits(IOMUX_GPIO_SEL(pin), 0x3fU << shift,
				    (uint32_t)pins[i].function << shift);
			update_bits(IOMUX_FULL_MUX_EN, BIT(pin), BIT(pin));
		}
		irq_unlock(key);
	}

	return 0;
}
