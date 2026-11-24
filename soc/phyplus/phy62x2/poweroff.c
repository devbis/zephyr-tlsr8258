/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/*
 * The power off of hal_pwrmgr_poweroff() of the PHY62XX SDK 3.1.1: the RAM is not retained, the
 * chip wakes on a level change of a pad and starts from the boot ROM, which finds the cause of the
 * start in AON->SLEEP_R[0]. The pads that wake it are the gpios of the enabled nodes of
 * phyplus,phy62x2-poweroff-wakeup: a pad that is active low wakes on the falling edge, a pad that
 * is active high on the rising edge.
 */
#define AON_PULL_BASE 0x4000f008U
#define AON_REG_S9 0x4000f0a0U
#define AON_REG_S10 0x4000f0a4U
#define AON_PMCTL2_0 0x4000f01cU
#define AON_SLEEP_R0 0x4000f0c0U
#define AON_POWER_OFF 0x4000f000U
#define GPIO_DIR 0x40008004U
#define GPIO_INT_TYPE 0x40008038U

#define RESET_CAUSE_OFF_MODE 2U
#define POWER_OFF_KEY 0x5a5aa5a5U
#define NUM_PINS 23U

/* The pad number of a pin of the GPIO controller, which is the bit of the wakeup enable */
static const uint8_t pad_number[NUM_PINS] = {0, 1, 2, 3, 7, 9, 10, 11, 14, 15, 16, 17,
					     18, 20, 23, 24, 25, 26, 27, 31, 32, 33, 34};
static const uint8_t pull_reg[NUM_PINS] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
					   1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3};
static const uint8_t pull_shift[NUM_PINS] = {1, 4, 7, 10, 22, 28, 1, 4, 13, 16, 19, 22,
					     25, 1, 10, 13, 16, 19, 22, 4, 7, 10, 13};

struct wake_pin {
	uint8_t pin;
	bool falling;
};

#define WAKEUP_COMPAT phyplus_phy62x2_poweroff_wakeup

#if DT_HAS_COMPAT_STATUS_OKAY(WAKEUP_COMPAT)
#define WAKE_PIN_INIT(node_id, prop, idx)                                                          \
	{.pin = DT_GPIO_PIN_BY_IDX(node_id, prop, idx),                                            \
	 .falling = (DT_GPIO_FLAGS_BY_IDX(node_id, prop, idx) & GPIO_ACTIVE_LOW) != 0},

#define WAKE_NODE_INIT(node_id) DT_FOREACH_PROP_ELEM(node_id, gpios, WAKE_PIN_INIT)

static const struct wake_pin wake_pins[] = {
	DT_FOREACH_STATUS_OKAY(WAKEUP_COMPAT, WAKE_NODE_INIT)
};
#define NUM_WAKE_PINS ARRAY_SIZE(wake_pins)
#else
static const struct wake_pin wake_pins[1];
#define NUM_WAKE_PINS 0U
#endif

static void update_bits(uintptr_t reg, uint32_t mask, uint32_t value)
{
	sys_write32((sys_read32(reg) & ~mask) | (value & mask), reg);
}

void z_sys_poweroff(void)
{
	(void)irq_lock();

	/* Disable the software control of the power */
	update_bits(AON_PMCTL2_0, BIT(6), 0U);

	for (size_t i = 0U; i < NUM_WAKE_PINS; i++) {
		const uint32_t pin = wake_pins[i].pin;
		/* The strong pull-up and the falling edge, or the pull-down and the rising edge */
		const uint32_t pull = wake_pins[i].falling ? 1U : 3U;
		const uint32_t polarity = pull_shift[pin] - 1U;

		update_bits(GPIO_DIR, BIT(pin), 0U);
		update_bits(AON_PULL_BASE + 4U * pull_reg[pin], 3U << pull_shift[pin],
			    pull << pull_shift[pin]);
		update_bits(GPIO_INT_TYPE, BIT(pin), BIT(pin));
		update_bits(AON_PULL_BASE + 4U * pull_reg[pin], BIT(polarity),
			    wake_pins[i].falling ? BIT(polarity) : 0U);
		if (pad_number[pin] < 32U) {
			update_bits(AON_REG_S9, BIT(pad_number[pin]), BIT(pad_number[pin]));
		} else {
			update_bits(AON_REG_S10, BIT(pad_number[pin] - 32U),
				    BIT(pad_number[pin] - 32U));
		}
	}

	/* The reset cause for the boot ROM, then the key that powers the chip off */
	sys_write32(RESET_CAUSE_OFF_MODE, AON_SLEEP_R0);
	sys_write32(POWER_OFF_KEY, AON_POWER_OFF);

	for (;;) {
	}
}
