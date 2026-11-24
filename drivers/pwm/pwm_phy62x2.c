/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT phyplus_phy62x2_pwm

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* Registers of the PHY62XX SDK 3.1.1: AP_PWM, AP_PWM_CTRL(n) and AP_PCR->SW_CLK */
#define PWM_BASE DT_INST_REG_ADDR(0)
#define PWM_EN (PWM_BASE + 0x00U)
#define PWM_CTRL0(n) (PWM_BASE + 0x04U + (n) * 12U)
#define PWM_CTRL1(n) (PWM_CTRL0(n) + 0x04U)
#define PWM_EN_ALL (BIT(0) | BIT(4))

#define CTRL0_ENABLE BIT(0)
#define CTRL0_POLARITY BIT(4)
#define CTRL0_DIV_SHIFT 12U
#define CTRL0_LOAD BIT(16)
#define CTRL0_INSTANT_LOAD BIT(31)

#define SW_CLK 0x40000008U
#define SW_CLK_PWM BIT(18)

#define NUM_CHANNELS 6U

struct phy62x2_pwm_config {
	uint32_t clock_frequency;
	uint8_t prescaler;
	const struct pinctrl_dev_config *pcfg;
};

struct phy62x2_pwm_data {
	uint8_t opened;
};

static void update_bits(uintptr_t reg, uint32_t mask, uint32_t value)
{
	sys_write32((sys_read32(reg) & ~mask) | (value & mask), reg);
}

static int phy62x2_pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
					  uint64_t *cycles)
{
	const struct phy62x2_pwm_config *config = dev->config;

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}

	*cycles = config->clock_frequency >> config->prescaler;
	return 0;
}

static int phy62x2_pwm_set_cycles(const struct device *dev, uint32_t channel, uint32_t period,
				  uint32_t pulse, pwm_flags_t flags)
{
	const struct phy62x2_pwm_config *config = dev->config;
	struct phy62x2_pwm_data *data = dev->data;
	uint32_t ctrl0;
	unsigned int key;

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}
	if (period == 0U || period > UINT16_MAX || pulse > period) {
		return -EINVAL;
	}

	ctrl0 = ((uint32_t)config->prescaler << CTRL0_DIV_SHIFT) |
		((flags & PWM_POLARITY_INVERTED) != 0U ? CTRL0_POLARITY : 0U);

	key = irq_lock();
	if ((data->opened & BIT(channel)) == 0U) {
		/* As hal_pwm_init() and hal_pwm_open_channel() of the SDK; the pad is set by pinctrl */
		sys_set_bits(SW_CLK, SW_CLK_PWM);
		sys_write32(ctrl0 | CTRL0_INSTANT_LOAD, PWM_CTRL0(channel));
		sys_write32(((pulse << 16) | period), PWM_CTRL1(channel));
		update_bits(PWM_CTRL0(channel), CTRL0_LOAD, CTRL0_LOAD);
		update_bits(PWM_CTRL0(channel), CTRL0_ENABLE, CTRL0_ENABLE);
		update_bits(PWM_EN, PWM_EN_ALL, PWM_EN_ALL);
		data->opened |= BIT(channel);
	} else {
		/* As hal_pwm_set_count_val(): the new values are loaded together */
		update_bits(PWM_CTRL0(channel), CTRL0_LOAD | CTRL0_POLARITY, ctrl0 & CTRL0_POLARITY);
		sys_write32(((pulse << 16) | period), PWM_CTRL1(channel));
		update_bits(PWM_CTRL0(channel), CTRL0_LOAD, CTRL0_LOAD);
	}
	irq_unlock(key);

	return 0;
}

static int phy62x2_pwm_init(const struct device *dev)
{
	const struct phy62x2_pwm_config *config = dev->config;

	return pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
}

static DEVICE_API(pwm, phy62x2_pwm_api) = {
	.set_cycles = phy62x2_pwm_set_cycles,
	.get_cycles_per_sec = phy62x2_pwm_get_cycles_per_sec,
};

PINCTRL_DT_INST_DEFINE(0);

static const struct phy62x2_pwm_config pwm_config = {
	.clock_frequency = DT_INST_PROP(0, clock_frequency),
	.prescaler = DT_INST_PROP(0, prescaler),
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

static struct phy62x2_pwm_data pwm_data;

DEVICE_DT_INST_DEFINE(0, phy62x2_pwm_init, NULL, &pwm_data, &pwm_config, POST_KERNEL,
		      CONFIG_PWM_INIT_PRIORITY, &phy62x2_pwm_api);
