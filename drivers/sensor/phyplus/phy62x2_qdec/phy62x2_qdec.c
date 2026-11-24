/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT phyplus_phy62x2_qdec

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* Registers of the PHY62XX SDK 3.1.1 (qdec.h), the channels X, Y and Z are 0x14 bytes apart */
#define QDEC_BASE DT_INST_REG_ADDR(0)
#define QDEC_ENABLE (QDEC_BASE + 0x00U)
#define QDEC_INT_ENABLE (QDEC_BASE + 0x04U)
#define QDEC_INT_CLEAR (QDEC_BASE + 0x08U)
#define QDEC_MODE(n) (QDEC_BASE + 0x10U + 0x14U * (n))
#define QDEC_COUNT(n) (QDEC_BASE + 0x1cU + 0x14U * (n))

#define SW_CLK 0x40000008U
#define SW_CLK_QDEC BIT(15)

#define FULL_ANGLE 360

struct phy62x2_qdec_config {
	uint32_t steps;
	uint8_t channel;
	uint8_t mode;
	const struct pinctrl_dev_config *pcfg;
};

struct phy62x2_qdec_data {
	int32_t fetched;
};

static int phy62x2_qdec_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct phy62x2_qdec_config *config = dev->config;
	struct phy62x2_qdec_data *data = dev->data;
	unsigned int key;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_ROTATION) {
		return -ENOTSUP;
	}

	/* As the interrupt handler of the SDK: read the counter, then clear the interrupt */
	key = irq_lock();
	data->fetched = (int32_t)sys_read32(QDEC_COUNT(config->channel));
	sys_write32(BIT(8U * config->channel), QDEC_INT_CLEAR);
	irq_unlock(key);

	return 0;
}

static int phy62x2_qdec_channel_get(const struct device *dev, enum sensor_channel chan,
				    struct sensor_value *val)
{
	const struct phy62x2_qdec_config *config = dev->config;
	struct phy62x2_qdec_data *data = dev->data;
	int32_t acc = data->fetched;

	if (chan != SENSOR_CHAN_ROTATION) {
		return -ENOTSUP;
	}

	val->val1 = (acc * FULL_ANGLE) / (int32_t)config->steps;
	val->val2 = (acc * FULL_ANGLE) - (val->val1 * (int32_t)config->steps);
	if (val->val2 != 0) {
		val->val2 *= 1000000;
		val->val2 /= (int32_t)config->steps;
	}

	return 0;
}

static int phy62x2_qdec_init(const struct device *dev)
{
	const struct phy62x2_qdec_config *config = dev->config;
	const uint32_t n = config->channel;
	int ret;

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	/* As qdec_hw_config() of the SDK: the interrupts of the channel off, the mode, the
	 * interrupt of the counter on for every change, the channel on. The interrupt line of the
	 * core stays off: the counter is polled.
	 */
	sys_set_bits(SW_CLK, SW_CLK_QDEC);
	sys_clear_bits(QDEC_INT_ENABLE, BIT(4U + 8U * n) | BIT(8U * n) | BIT(25U + 2U * n) |
					BIT(24U + 2U * n));
	sys_write32(config->mode, QDEC_MODE(n));
	sys_clear_bits(QDEC_INT_ENABLE, BIT(2U + 8U * n));
	sys_set_bits(QDEC_INT_ENABLE, BIT(8U * n));
	sys_set_bits(QDEC_ENABLE, BIT(4U * n));

	return 0;
}

static DEVICE_API(sensor, phy62x2_qdec_api) = {
	.sample_fetch = phy62x2_qdec_sample_fetch,
	.channel_get = phy62x2_qdec_channel_get,
};

PINCTRL_DT_INST_DEFINE(0);

static const struct phy62x2_qdec_config qdec_config = {
	.steps = DT_INST_PROP(0, steps),
	.channel = DT_INST_PROP(0, channel),
	/* 1x, 2x and 4x are 1, 2 and 3 in the mode register */
	.mode = DT_INST_PROP(0, quadrature_mode) == 4 ? 3 : DT_INST_PROP(0, quadrature_mode),
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0),
};

static struct phy62x2_qdec_data qdec_data;

SENSOR_DEVICE_DT_INST_DEFINE(0, phy62x2_qdec_init, NULL, &qdec_data, &qdec_config, POST_KERNEL,
			     CONFIG_SENSOR_INIT_PRIORITY, &phy62x2_qdec_api);
