/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Telink TLSR8258 PWM controller driver.
 *
 * Six continuous-mode channels (PWM0..PWM5) share one clock divider. A signal
 * frame lasts TMAX ticks and the pulse lasts TCMP ticks; both are 16-bit and
 * packed in one 32-bit register (TCMP in the low half). Register writes take
 * effect at the next frame boundary.
 *
 * Not implemented:
 * - PWMn_N outputs are not exposed as separate channels. They are selected by
 *   pinmux only and follow the channel waveform with the PWM_CC1 inversion
 *   bit, which the driver leaves cleared.
 * - PWM0 IR, counting and IR FIFO/DMA modes. Only normal mode is used.
 * - The vendor pwm_kick_32k_pad_times() path (proj/drivers/drv_pm.c) drives
 *   PWM1 to kick the 32k pad during power management. It is not part of the
 *   PWM API and is left to the PM and 32k clock code.
 */

#define DT_DRV_COMPAT telink_tlsr8258_pwm

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#define PWM_TLSR8258_CHANNELS 6u

/* Offsets from the PWM block base (0x00800780). */
#define PWM_TLSR8258_EN_CH1_5 0x00u /* PWM_EN 0x780, bit n enables PWMn */
#define PWM_TLSR8258_EN_CH0   0x01u /* PWM_EN0 0x781, bit 0 enables PWM0 */
#define PWM_TLSR8258_CLKDIV   0x02u /* PWM_CLKDIV 0x782 */
#define PWM_TLSR8258_MODE     0x03u /* PWM_MODE 0x783, PWM0 mode select */
#define PWM_TLSR8258_INV      0x04u /* PWM_CC0 0x784, invert PWMn */
#define PWM_TLSR8258_INV_N    0x05u /* PWM_CC1 0x785, invert PWMn_N */
#define PWM_TLSR8258_POL      0x06u /* PWM_CC2 0x786, polarity per channel */
#define PWM_TLSR8258_CYCLE(ch) (0x14u + 4u * (ch)) /* 0x794: TCMP | TMAX << 16 */

#define PWM_TLSR8258_REG8(base, off)  (*(volatile uint8_t *)((base) + (off)))
#define PWM_TLSR8258_REG32(base, off) (*(volatile uint32_t *)((base) + (off)))

struct pwm_tlsr8258_config {
	uintptr_t base;
	const struct pinctrl_dev_config *pcfg;
	uint32_t clock_frequency;
	uint32_t prescaler;
};

/* Read-modify-write of a shared register byte, serialized against IRQs. */
static void pwm_tlsr8258_update8(uintptr_t base, uint32_t off, uint8_t bit, bool set)
{
	unsigned int key = irq_lock();

	if (set) {
		PWM_TLSR8258_REG8(base, off) |= bit;
	} else {
		PWM_TLSR8258_REG8(base, off) &= (uint8_t)~bit;
	}

	irq_unlock(key);
}

static void pwm_tlsr8258_enable(uintptr_t base, uint32_t channel, bool enable)
{
	if (channel == 0u) {
		pwm_tlsr8258_update8(base, PWM_TLSR8258_EN_CH0, BIT(0), enable);
	} else {
		pwm_tlsr8258_update8(base, PWM_TLSR8258_EN_CH1_5, BIT(channel), enable);
	}
}

static int pwm_tlsr8258_set_cycles(const struct device *dev, uint32_t channel,
				   uint32_t period_cycles, uint32_t pulse_cycles,
				   pwm_flags_t flags)
{
	const struct pwm_tlsr8258_config *config = dev->config;

	if (channel >= PWM_TLSR8258_CHANNELS) {
		return -EINVAL;
	}

	if ((period_cycles == 0u) || (period_cycles > UINT16_MAX) ||
	    (pulse_cycles > period_cycles)) {
		return -EINVAL;
	}

	pwm_tlsr8258_update8(config->base, PWM_TLSR8258_POL, BIT(channel),
			     (flags & PWM_POLARITY_INVERTED) != 0u);

	/* One 32-bit store so TCMP and TMAX always belong to the same frame. */
	PWM_TLSR8258_REG32(config->base, PWM_TLSR8258_CYCLE(channel)) =
		((uint32_t)period_cycles << 16) | pulse_cycles;

	pwm_tlsr8258_enable(config->base, channel, true);

	return 0;
}

static int pwm_tlsr8258_get_cycles_per_sec(const struct device *dev, uint32_t channel,
					   uint64_t *cycles)
{
	const struct pwm_tlsr8258_config *config = dev->config;

	if (channel >= PWM_TLSR8258_CHANNELS) {
		return -EINVAL;
	}

	*cycles = (uint64_t)config->clock_frequency / ((uint64_t)config->prescaler + 1u);

	return 0;
}

static int pwm_tlsr8258_init(const struct device *dev)
{
	const struct pwm_tlsr8258_config *config = dev->config;
	uintptr_t base = config->base;

	if (config->prescaler > UINT8_MAX) {
		return -EINVAL;
	}

	if (config->pcfg != NULL) {
		int ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

		if (ret < 0) {
			return ret;
		}
	}

	/* Known state: all channels stopped, normal mode, no inversion. */
	PWM_TLSR8258_REG8(base, PWM_TLSR8258_EN_CH1_5) = 0u;
	PWM_TLSR8258_REG8(base, PWM_TLSR8258_EN_CH0) = 0u;
	PWM_TLSR8258_REG8(base, PWM_TLSR8258_CLKDIV) = (uint8_t)config->prescaler;
	PWM_TLSR8258_REG8(base, PWM_TLSR8258_MODE) = 0u;
	PWM_TLSR8258_REG8(base, PWM_TLSR8258_INV) = 0u;
	PWM_TLSR8258_REG8(base, PWM_TLSR8258_INV_N) = 0u;
	PWM_TLSR8258_REG8(base, PWM_TLSR8258_POL) = 0u;

	return 0;
}

static DEVICE_API(pwm, pwm_tlsr8258_api) = {
	.set_cycles = pwm_tlsr8258_set_cycles,
	.get_cycles_per_sec = pwm_tlsr8258_get_cycles_per_sec,
};

#define PWM_TLSR8258_PINCTRL_DEFINE(n) \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, pinctrl_0), (PINCTRL_DT_INST_DEFINE(n);), ())

#define PWM_TLSR8258_PCFG(n) \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, pinctrl_0), \
		    (PINCTRL_DT_INST_DEV_CONFIG_GET(n)), (NULL))

#define PWM_TLSR8258_INIT(n)                                                    \
	PWM_TLSR8258_PINCTRL_DEFINE(n)                                          \
									\
	static const struct pwm_tlsr8258_config pwm_tlsr8258_cfg_##n = {        \
		.base = DT_INST_REG_ADDR(n),                                    \
		.pcfg = PWM_TLSR8258_PCFG(n),                                   \
		.clock_frequency = DT_PROP(DT_INST_CLOCKS_CTLR(n), clock_frequency), \
		.prescaler = DT_INST_PROP(n, prescaler),                        \
	};                                                                      \
									\
	DEVICE_DT_INST_DEFINE(n, pwm_tlsr8258_init, NULL, NULL,                 \
			      &pwm_tlsr8258_cfg_##n, POST_KERNEL,               \
			      CONFIG_PWM_INIT_PRIORITY, &pwm_tlsr8258_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_TLSR8258_INIT)
