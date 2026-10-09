/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Counter driver for the TLSR8258 hardware timers 0, 1 and 2.
 *
 * Each timer counts up from its tick register at the timer clock while its
 * enable bit is set. A single capture register holds the compare value: the
 * interrupt is raised when tick equals capture. The timer index is the
 * interrupt number (TMR0 = IRQ 0, TMR1 = IRQ 1, TMR2 = IRQ 2).
 *
 * The capture register serves either one channel alarm or the top value, not
 * both at once. A top value makes the counter restart from zero on every
 * match and call the top callback. Alarms are one-shot.
 *
 * Only SYSCLK mode is used. GPIO trigger and width modes are not implemented.
 */

#define DT_DRV_COMPAT telink_tlsr8258_timer

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <tlsr82xx/irq.h>

/* Offsets from the timer block at 0x00800620 (reg_tmr_ctrl). */
#define TLSR8258_TMR_CTRL_OFF        0x00U
#define TLSR8258_TMR_STA_OFF         0x03U
#define TLSR8258_TMR_CAPT_OFF(idx)   (0x04U + 4U * (idx))
#define TLSR8258_TMR_TICK_OFF(idx)   (0x10U + 4U * (idx))

/* Per-timer fields of reg_tmr_ctrl: timer n uses bit 3n for enable and two
 * bits starting at 3n + 1 for the mode.
 */
#define TLSR8258_TMR_EN(idx)         BIT(3U * (idx))
#define TLSR8258_TMR_MODE_MASK(idx)  (GENMASK(2, 1) << (3U * (idx)))

/* Alarms closer than this to the current tick are reported as late. */
#define TLSR8258_TMR_MIN_ALARM_TICKS 2

/* The vendor SDK and the MCU ISR path both use IRQ n for timer n. The
 * watchdog runs on timer 2 and the vendor IRQ offload on timer 0.
 */
#define TLSR8258_TMR_IRQ_MAX         2

struct counter_tlsr8258_config {
	struct counter_config_info counter_info;
	uintptr_t base;
	uint8_t idx;
	uint8_t irq;
	void (*irq_config)(const struct device *dev);
};

struct counter_tlsr8258_data {
	counter_alarm_callback_t alarm_cb;
	void *alarm_user_data;
	bool alarm_active;
	counter_top_callback_t top_cb;
	void *top_user_data;
	uint32_t top_ticks;
	bool top_active;
};

static inline volatile uint32_t *tlsr8258_tmr_reg32(const struct counter_tlsr8258_config *cfg,
						    uint32_t off)
{
	return (volatile uint32_t *)(cfg->base + off);
}

static inline volatile uint8_t *tlsr8258_tmr_reg8(const struct counter_tlsr8258_config *cfg,
						  uint32_t off)
{
	return (volatile uint8_t *)(cfg->base + off);
}

/* reg_tmr_ctrl is accessed one byte at a time. Bits 24..31 hold the status
 * flags, and a word write would touch the status of the other timers.
 */
static uint32_t tlsr8258_tmr_ctrl_read(const struct counter_tlsr8258_config *cfg)
{
	volatile uint8_t *ctrl = tlsr8258_tmr_reg8(cfg, TLSR8258_TMR_CTRL_OFF);

	return (uint32_t)ctrl[0] | ((uint32_t)ctrl[1] << 8) | ((uint32_t)ctrl[2] << 16);
}

static void tlsr8258_tmr_ctrl_write(const struct counter_tlsr8258_config *cfg, uint32_t value)
{
	volatile uint8_t *ctrl = tlsr8258_tmr_reg8(cfg, TLSR8258_TMR_CTRL_OFF);

	ctrl[0] = (uint8_t)value;
	ctrl[1] = (uint8_t)(value >> 8);
	ctrl[2] = (uint8_t)(value >> 16);
}

static void tlsr8258_tmr_ctrl_modify(const struct counter_tlsr8258_config *cfg, uint32_t clear,
				     uint32_t set)
{
	unsigned int key = irq_lock();
	uint32_t ctrl = tlsr8258_tmr_ctrl_read(cfg);

	ctrl = (ctrl & ~clear) | set;
	tlsr8258_tmr_ctrl_write(cfg, ctrl);
	irq_unlock(key);
}

static void tlsr8258_tmr_clear_status(const struct counter_tlsr8258_config *cfg)
{
	/* Write-one-to-clear. Only this timer's bit is written. */
	*tlsr8258_tmr_reg8(cfg, TLSR8258_TMR_STA_OFF) = (uint8_t)BIT(cfg->idx);
}

static uint32_t tlsr8258_tmr_get_tick(const struct counter_tlsr8258_config *cfg)
{
	return *tlsr8258_tmr_reg32(cfg, TLSR8258_TMR_TICK_OFF(cfg->idx));
}

static void tlsr8258_tmr_set_tick(const struct counter_tlsr8258_config *cfg, uint32_t tick)
{
	*tlsr8258_tmr_reg32(cfg, TLSR8258_TMR_TICK_OFF(cfg->idx)) = tick;
}

static void tlsr8258_tmr_set_capture(const struct counter_tlsr8258_config *cfg, uint32_t capt)
{
	*tlsr8258_tmr_reg32(cfg, TLSR8258_TMR_CAPT_OFF(cfg->idx)) = capt;
}

static int counter_tlsr8258_start(const struct device *dev)
{
	const struct counter_tlsr8258_config *cfg = dev->config;

	tlsr8258_tmr_ctrl_modify(cfg, 0U, TLSR8258_TMR_EN(cfg->idx));

	return 0;
}

static int counter_tlsr8258_stop(const struct device *dev)
{
	const struct counter_tlsr8258_config *cfg = dev->config;

	tlsr8258_tmr_ctrl_modify(cfg, TLSR8258_TMR_EN(cfg->idx), 0U);

	return 0;
}

static int counter_tlsr8258_get_value(const struct device *dev, uint32_t *ticks)
{
	const struct counter_tlsr8258_config *cfg = dev->config;

	*ticks = tlsr8258_tmr_get_tick(cfg);

	return 0;
}

static int counter_tlsr8258_set_alarm(const struct device *dev, uint8_t chan_id,
				      const struct counter_alarm_cfg *alarm_cfg)
{
	const struct counter_tlsr8258_config *cfg = dev->config;
	struct counter_tlsr8258_data *data = dev->data;
	uint32_t now;
	uint32_t target;
	int ret = 0;

	if (chan_id != 0U) {
		return -ENOTSUP;
	}

	unsigned int key = irq_lock();

	if (data->alarm_active || data->top_active) {
		ret = -EBUSY;
		goto out;
	}

	now = tlsr8258_tmr_get_tick(cfg);
	if ((alarm_cfg->flags & COUNTER_ALARM_CFG_ABSOLUTE) != 0U) {
		target = alarm_cfg->ticks;
	} else {
		target = now + alarm_cfg->ticks;
	}

	if ((int32_t)(target - now) < TLSR8258_TMR_MIN_ALARM_TICKS) {
		if ((alarm_cfg->flags & COUNTER_ALARM_CFG_EXPIRE_WHEN_LATE) == 0U) {
			ret = -ETIME;
			goto out;
		}
		target = now + TLSR8258_TMR_MIN_ALARM_TICKS;
	}

	data->alarm_cb = alarm_cfg->callback;
	data->alarm_user_data = alarm_cfg->user_data;
	data->alarm_active = true;

	tlsr8258_tmr_clear_status(cfg);
	tlsr8258_tmr_set_capture(cfg, target);
	irq_enable(cfg->irq);

out:
	irq_unlock(key);

	return ret;
}

static int counter_tlsr8258_cancel_alarm(const struct device *dev, uint8_t chan_id)
{
	const struct counter_tlsr8258_config *cfg = dev->config;
	struct counter_tlsr8258_data *data = dev->data;

	if (chan_id != 0U) {
		return -ENOTSUP;
	}

	unsigned int key = irq_lock();

	if (data->alarm_active) {
		irq_disable(cfg->irq);
		tlsr8258_tmr_clear_status(cfg);
		data->alarm_active = false;
		data->alarm_cb = NULL;
	}
	irq_unlock(key);

	return 0;
}

static int counter_tlsr8258_set_top_value(const struct device *dev,
					  const struct counter_top_cfg *top_cfg)
{
	const struct counter_tlsr8258_config *cfg = dev->config;
	struct counter_tlsr8258_data *data = dev->data;
	int ret = 0;

	if (top_cfg->ticks == 0U) {
		return -EINVAL;
	}

	unsigned int key = irq_lock();

	if (data->alarm_active) {
		ret = -EBUSY;
		goto out;
	}

	if ((top_cfg->flags & COUNTER_TOP_CFG_DONT_RESET) != 0U) {
		if (tlsr8258_tmr_get_tick(cfg) > top_cfg->ticks) {
			ret = -ETIME;
			goto out;
		}
	} else {
		tlsr8258_tmr_set_tick(cfg, 0U);
	}

	data->top_ticks = top_cfg->ticks;
	data->top_cb = top_cfg->callback;
	data->top_user_data = top_cfg->user_data;
	data->top_active = true;

	tlsr8258_tmr_clear_status(cfg);
	tlsr8258_tmr_set_capture(cfg, top_cfg->ticks);
	irq_enable(cfg->irq);

out:
	irq_unlock(key);

	return ret;
}

static uint32_t counter_tlsr8258_get_top_value(const struct device *dev)
{
	struct counter_tlsr8258_data *data = dev->data;

	return data->top_ticks;
}

static uint32_t counter_tlsr8258_get_pending_int(const struct device *dev)
{
	const struct counter_tlsr8258_config *cfg = dev->config;

	return (*tlsr8258_tmr_reg8(cfg, TLSR8258_TMR_STA_OFF) >> cfg->idx) & 0x1U;
}

static void counter_tlsr8258_isr(const void *arg)
{
	const struct device *dev = arg;
	const struct counter_tlsr8258_config *cfg = dev->config;
	struct counter_tlsr8258_data *data = dev->data;

	/* The arch dispatcher has already cleared the timer status. */
	if (data->top_active) {
		tlsr8258_tmr_set_tick(cfg, 0U);
		tlsr8258_tmr_set_capture(cfg, data->top_ticks);
		if (data->top_cb != NULL) {
			data->top_cb(dev, data->top_user_data);
		}
	} else if (data->alarm_active) {
		counter_alarm_callback_t cb = data->alarm_cb;
		void *user_data = data->alarm_user_data;

		irq_disable(cfg->irq);
		data->alarm_active = false;
		data->alarm_cb = NULL;
		if (cb != NULL) {
			cb(dev, 0U, tlsr8258_tmr_get_tick(cfg), user_data);
		}
	}
}

static int counter_tlsr8258_init(const struct device *dev)
{
	const struct counter_tlsr8258_config *cfg = dev->config;
	struct counter_tlsr8258_data *data = dev->data;

	irq_disable(cfg->irq);
	tlsr8258_tmr_ctrl_modify(cfg, TLSR8258_TMR_EN(cfg->idx) | TLSR8258_TMR_MODE_MASK(cfg->idx),
				 0U);
	tlsr8258_tmr_set_tick(cfg, 0U);
	tlsr8258_tmr_set_capture(cfg, 0U);
	tlsr8258_tmr_clear_status(cfg);

	data->alarm_cb = NULL;
	data->alarm_user_data = NULL;
	data->alarm_active = false;
	data->top_cb = NULL;
	data->top_user_data = NULL;
	data->top_ticks = UINT32_MAX;
	data->top_active = false;

	cfg->irq_config(dev);

	return 0;
}

static DEVICE_API(counter, counter_tlsr8258_api) = {
	.start = counter_tlsr8258_start,
	.stop = counter_tlsr8258_stop,
	.get_value = counter_tlsr8258_get_value,
	.set_alarm = counter_tlsr8258_set_alarm,
	.cancel_alarm = counter_tlsr8258_cancel_alarm,
	.set_top_value = counter_tlsr8258_set_top_value,
	.get_pending_int = counter_tlsr8258_get_pending_int,
	.get_top_value = counter_tlsr8258_get_top_value,
};

#define COUNTER_TLSR8258_INIT(n)                                                                  \
	BUILD_ASSERT(DT_INST_IRQN(n) <= TLSR8258_TMR_IRQ_MAX,                                     \
		     "telink,tlsr8258-timer: interrupts must be 0, 1 or 2 (timer index)");        \
	BUILD_ASSERT(DT_INST_IRQN(n) != 0 || !IS_ENABLED(CONFIG_IRQ_OFFLOAD),                     \
		     "TLSR8258 timer 0 is used by IRQ offload");                                  \
	BUILD_ASSERT(DT_INST_IRQN(n) != 2 || !IS_ENABLED(CONFIG_WDT_TLSR8258),                    \
		     "TLSR8258 timer 2 is used by the watchdog");                                 \
	BUILD_ASSERT(DT_INST_IRQN(n) != 1 || !IS_ENABLED(CONFIG_SOC_SERIES_TLSR825X_PM),          \
		     "TLSR8258 timer 1 is used by the power management code");                    \
	static void counter_tlsr8258_irq_config_##n(const struct device *dev)                     \
	{                                                                                         \
		ARG_UNUSED(dev);                                                                  \
		IRQ_CONNECT(DT_INST_IRQN(n), 0, counter_tlsr8258_isr, DEVICE_DT_INST_GET(n), 0);  \
	}                                                                                         \
	static const struct counter_tlsr8258_config counter_tlsr8258_config_##n = {               \
		.counter_info = {                                                                 \
			.freq = DT_INST_PROP(n, clock_frequency),                                 \
			.max_top_value = UINT32_MAX,                                              \
			.flags = COUNTER_CONFIG_INFO_COUNT_UP,                                    \
			.channels = 1U,                                                           \
		},                                                                                \
		.base = DT_INST_REG_ADDR(n),                                                      \
		.idx = DT_INST_IRQN(n),                                                           \
		.irq = DT_INST_IRQN(n),                                                           \
		.irq_config = counter_tlsr8258_irq_config_##n,                                    \
	};                                                                                        \
	static struct counter_tlsr8258_data counter_tlsr8258_data_##n;                            \
	DEVICE_DT_INST_DEFINE(n, counter_tlsr8258_init, NULL, &counter_tlsr8258_data_##n,         \
			      &counter_tlsr8258_config_##n, POST_KERNEL,                          \
			      CONFIG_COUNTER_INIT_PRIORITY, &counter_tlsr8258_api);

DT_INST_FOREACH_STATUS_OKAY(COUNTER_TLSR8258_INIT)
