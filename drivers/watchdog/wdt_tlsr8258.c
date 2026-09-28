/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_watchdog

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

/*
 * The watchdog is part of the timer block. reg_tmr_ctrl is 24 bits wide
 * (0x620..0x622) and the byte above it is reg_tmr_sta (0x623), whose bits are
 * write-one-to-clear. Access the control register byte by byte so that no
 * write reaches the status bits of the other timers.
 */
#define WDT_TLSR8258_TMR_CTRL(base)  ((volatile uint8_t *)(base))
#define WDT_TLSR8258_TMR_STA(base)   ((volatile uint8_t *)((base) + 0x03U))
#define WDT_TLSR8258_TMR2_TICK(base) ((volatile uint32_t *)((base) + 0x18U))

#define WDT_TLSR8258_TMR2_EN       BIT(6)
#define WDT_TLSR8258_WD_CAPT_SHIFT 9U
#define WDT_TLSR8258_WD_CAPT_MASK  GENMASK(22, 9)
#define WDT_TLSR8258_WD_EN         BIT(23)
#define WDT_TLSR8258_STA_WD        BIT(3)

/* The capture field is compared against bits [31:18] of the timer2 tick. */
#define WDT_TLSR8258_CAPT_TICK_SHIFT 18U
#define WDT_TLSR8258_CAPT_MAX        (WDT_TLSR8258_WD_CAPT_MASK >> WDT_TLSR8258_WD_CAPT_SHIFT)

struct wdt_tlsr8258_config {
	uintptr_t base;
	uint32_t clock_frequency;
};

struct wdt_tlsr8258_data {
	uint32_t capture;
	bool installed;
	bool enabled;
};

static uint32_t wdt_tlsr8258_ctrl_read(uintptr_t base)
{
	volatile uint8_t *ctrl = WDT_TLSR8258_TMR_CTRL(base);

	return (uint32_t)ctrl[0] | ((uint32_t)ctrl[1] << 8) | ((uint32_t)ctrl[2] << 16);
}

static void wdt_tlsr8258_ctrl_write(uintptr_t base, uint32_t value)
{
	volatile uint8_t *ctrl = WDT_TLSR8258_TMR_CTRL(base);

	ctrl[0] = (uint8_t)value;
	ctrl[1] = (uint8_t)(value >> 8);
	ctrl[2] = (uint8_t)(value >> 16);
}

static void wdt_tlsr8258_stop(uintptr_t base)
{
	unsigned int key = irq_lock();
	uint32_t ctrl = wdt_tlsr8258_ctrl_read(base);

	ctrl &= ~(WDT_TLSR8258_TMR2_EN | WDT_TLSR8258_WD_EN);
	wdt_tlsr8258_ctrl_write(base, ctrl);
	irq_unlock(key);

	*WDT_TLSR8258_TMR2_TICK(base) = 0U;
	*WDT_TLSR8258_TMR_STA(base) = WDT_TLSR8258_STA_WD;
}

static void wdt_tlsr8258_start(uintptr_t base, uint32_t capture)
{
	unsigned int key = irq_lock();
	uint32_t ctrl = wdt_tlsr8258_ctrl_read(base);

	ctrl &= ~WDT_TLSR8258_WD_CAPT_MASK;
	ctrl |= (capture << WDT_TLSR8258_WD_CAPT_SHIFT) & WDT_TLSR8258_WD_CAPT_MASK;
	wdt_tlsr8258_ctrl_write(base, ctrl);
	*WDT_TLSR8258_TMR2_TICK(base) = 0U;
	wdt_tlsr8258_ctrl_write(base, ctrl | WDT_TLSR8258_TMR2_EN | WDT_TLSR8258_WD_EN);
	irq_unlock(key);
}

static int wdt_tlsr8258_setup(const struct device *dev, uint8_t options)
{
	const struct wdt_tlsr8258_config *cfg = dev->config;
	struct wdt_tlsr8258_data *data = dev->data;

	if (!data->installed) {
		return -EINVAL;
	}
	if (options != 0U) {
		return -ENOTSUP;
	}

	wdt_tlsr8258_stop(cfg->base);
	wdt_tlsr8258_start(cfg->base, data->capture);
	data->enabled = true;

	return 0;
}

static int wdt_tlsr8258_disable(const struct device *dev)
{
	const struct wdt_tlsr8258_config *cfg = dev->config;
	struct wdt_tlsr8258_data *data = dev->data;

	wdt_tlsr8258_stop(cfg->base);
	data->installed = false;
	data->enabled = false;

	return 0;
}

static int wdt_tlsr8258_install_timeout(const struct device *dev,
					const struct wdt_timeout_cfg *config)
{
	const struct wdt_tlsr8258_config *cfg = dev->config;
	struct wdt_tlsr8258_data *data = dev->data;
	uint64_t capture;

	if (data->enabled) {
		return -EBUSY;
	}
	if (data->installed) {
		return -ENOMEM;
	}
	if ((config->callback != NULL) || (config->window.min != 0U)) {
		return -ENOTSUP;
	}
	if ((config->flags & WDT_FLAG_RESET_MASK) != WDT_FLAG_RESET_SOC) {
		return -ENOTSUP;
	}

	capture = ((uint64_t)config->window.max * (cfg->clock_frequency / 1000U)) >>
		  WDT_TLSR8258_CAPT_TICK_SHIFT;
	if ((capture == 0U) || (capture > WDT_TLSR8258_CAPT_MAX)) {
		return -EINVAL;
	}

	data->capture = (uint32_t)capture;
	data->installed = true;

	return 0;
}

static int wdt_tlsr8258_feed(const struct device *dev, int channel_id)
{
	const struct wdt_tlsr8258_config *cfg = dev->config;
	struct wdt_tlsr8258_data *data = dev->data;

	if ((channel_id != 0) || !data->enabled) {
		return -EINVAL;
	}

	*WDT_TLSR8258_TMR_STA(cfg->base) = WDT_TLSR8258_STA_WD;

	return 0;
}

static DEVICE_API(wdt, wdt_tlsr8258_api) = {
	.setup = wdt_tlsr8258_setup,
	.disable = wdt_tlsr8258_disable,
	.install_timeout = wdt_tlsr8258_install_timeout,
	.feed = wdt_tlsr8258_feed,
};

static int wdt_tlsr8258_init(const struct device *dev)
{
	const struct wdt_tlsr8258_config *cfg = dev->config;

	/*
	 * A watchdog reset leaves the enable bit and the running tick in place,
	 * so the watchdog would fire again early in the next boot. Stop it here;
	 * the application arms it again through the watchdog API.
	 */
	wdt_tlsr8258_stop(cfg->base);

	return 0;
}

static const struct wdt_tlsr8258_config wdt_tlsr8258_cfg = {
	.base = DT_INST_REG_ADDR(0),
	.clock_frequency = DT_INST_PROP(0, clock_frequency),
};

static struct wdt_tlsr8258_data wdt_tlsr8258_data;

DEVICE_DT_INST_DEFINE(0, wdt_tlsr8258_init, NULL, &wdt_tlsr8258_data, &wdt_tlsr8258_cfg,
		      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &wdt_tlsr8258_api);
