/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT phyplus_phy62x2_rtc

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/*
 * The counter of the always-on RTC of the PHY62x2. The control register is used as the rtc_*
 * functions of the boot ROM use it: bit 0 runs the counter, bit 1 clears it, bits 13:2 hold the
 * prescaler minus one. The counter is read twice until the two reads agree, as rtc_get_counter()
 * does, because it is clocked from another domain.
 *
 * The comparators and the interrupt of the RTC are not used: neither the SDK nor the ROM handles
 * them, the ROM only arms RTCCC0 to wake the chip from the sleep (config_RTC), which is what the
 * power management does. The alarms are therefore not supported. The width of the counter, 24 bits,
 * is assumed.
 */
#define RTC_BASE DT_INST_REG_ADDR(0)
#define RTC_CTL (RTC_BASE + 0x00U)
#define RTC_CNT (RTC_BASE + 0x04U)

#define RTC_CTL_RUN BIT(0)
#define RTC_CTL_CLEAR BIT(1)
#define RTC_CTL_PRESCALER_MASK GENMASK(13, 2)
#define RTC_CTL_PRESCALER_SHIFT 2U

#define RTC_COUNTER_BITS 24U

struct phy62x2_rtc_config {
	struct counter_config_info info;
	uint32_t prescaler;
};

static int phy62x2_rtc_start(const struct device *dev)
{
	ARG_UNUSED(dev);
	sys_set_bits(RTC_CTL, RTC_CTL_RUN);

	return 0;
}

static int phy62x2_rtc_stop(const struct device *dev)
{
	ARG_UNUSED(dev);
	sys_clear_bits(RTC_CTL, RTC_CTL_RUN);

	return 0;
}

static int phy62x2_rtc_get_value(const struct device *dev, uint32_t *ticks)
{
	uint32_t first;
	uint32_t second;

	ARG_UNUSED(dev);
	do {
		first = sys_read32(RTC_CNT);
		second = sys_read32(RTC_CNT);
	} while (first != second);
	*ticks = second & BIT_MASK(RTC_COUNTER_BITS);

	return 0;
}

static int phy62x2_rtc_reset(const struct device *dev)
{
	ARG_UNUSED(dev);
	sys_set_bits(RTC_CTL, RTC_CTL_CLEAR);

	return 0;
}

static int phy62x2_rtc_set_alarm(const struct device *dev, uint8_t chan_id,
				 const struct counter_alarm_cfg *alarm_cfg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan_id);
	ARG_UNUSED(alarm_cfg);

	return -ENOTSUP;
}

static int phy62x2_rtc_cancel_alarm(const struct device *dev, uint8_t chan_id)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan_id);

	return -ENOTSUP;
}

static int phy62x2_rtc_set_top_value(const struct device *dev, const struct counter_top_cfg *cfg)
{
	ARG_UNUSED(dev);

	/* The counter always wraps at its width */
	return cfg->ticks == BIT_MASK(RTC_COUNTER_BITS) ? 0 : -ENOTSUP;
}

static uint32_t phy62x2_rtc_get_pending_int(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0U;
}

static uint32_t phy62x2_rtc_get_top_value(const struct device *dev)
{
	ARG_UNUSED(dev);

	return BIT_MASK(RTC_COUNTER_BITS);
}

static int phy62x2_rtc_init(const struct device *dev)
{
	const struct phy62x2_rtc_config *config = dev->config;
	const uint32_t wanted = (config->prescaler - 1U) << RTC_CTL_PRESCALER_SHIFT;
	const uint32_t ctl = sys_read32(RTC_CTL);

	/* As rtc_config_prescale(): stop, change the prescaler if it differs, run */
	if ((ctl & RTC_CTL_PRESCALER_MASK) != wanted) {
		sys_write32(ctl & ~RTC_CTL_RUN, RTC_CTL);
		sys_write32((sys_read32(RTC_CTL) & ~RTC_CTL_PRESCALER_MASK) | wanted, RTC_CTL);
	}
	sys_set_bits(RTC_CTL, RTC_CTL_RUN);

	return 0;
}

static DEVICE_API(counter, phy62x2_rtc_api) = {
	.start = phy62x2_rtc_start,
	.stop = phy62x2_rtc_stop,
	.get_value = phy62x2_rtc_get_value,
	.reset = phy62x2_rtc_reset,
	.set_alarm = phy62x2_rtc_set_alarm,
	.cancel_alarm = phy62x2_rtc_cancel_alarm,
	.set_top_value = phy62x2_rtc_set_top_value,
	.get_pending_int = phy62x2_rtc_get_pending_int,
	.get_top_value = phy62x2_rtc_get_top_value,
};

static const struct phy62x2_rtc_config rtc_config = {
	.info = {
		.max_top_value = BIT_MASK(RTC_COUNTER_BITS),
		.freq = DT_INST_PROP(0, clock_frequency) / DT_INST_PROP(0, prescaler),
		.flags = 0,
		.channels = 0,
	},
	.prescaler = DT_INST_PROP(0, prescaler),
};

DEVICE_DT_INST_DEFINE(0, phy62x2_rtc_init, NULL, NULL, &rtc_config, PRE_KERNEL_1,
		      CONFIG_COUNTER_INIT_PRIORITY, &phy62x2_rtc_api);
