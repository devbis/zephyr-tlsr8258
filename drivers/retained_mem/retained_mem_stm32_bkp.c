/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_stm32_bkp_retained

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/sys/util.h>

#include <soc.h>
#include <stm32_backup_domain.h>

struct stm32_bkp_retained_config {
	uint8_t first;
	size_t size;
};

static volatile uint32_t *stm32_bkp_reg(const struct stm32_bkp_retained_config *config,
					size_t index)
{
	return &(&RTC->BKP0R)[config->first + index];
}

static ssize_t stm32_bkp_retained_size(const struct device *dev)
{
	const struct stm32_bkp_retained_config *config = dev->config;

	return (ssize_t)config->size;
}

static int stm32_bkp_retained_read(const struct device *dev, off_t offset, uint8_t *buffer,
				   size_t size)
{
	const struct stm32_bkp_retained_config *config = dev->config;

	for (size_t i = 0; i < size; i++) {
		size_t pos = (size_t)offset + i;
		uint32_t word = *stm32_bkp_reg(config, pos / 4U);

		buffer[i] = (uint8_t)(word >> (8U * (pos % 4U)));
	}

	return 0;
}

static int stm32_bkp_retained_write(const struct device *dev, off_t offset,
				    const uint8_t *buffer, size_t size)
{
	const struct stm32_bkp_retained_config *config = dev->config;

	stm32_backup_domain_enable_access();

	for (size_t i = 0; i < size; i++) {
		size_t pos = (size_t)offset + i;
		volatile uint32_t *reg = stm32_bkp_reg(config, pos / 4U);
		uint32_t shift = 8U * (pos % 4U);

		*reg = (*reg & ~(0xffU << shift)) | ((uint32_t)buffer[i] << shift);
	}

	stm32_backup_domain_disable_access();

	return 0;
}

static int stm32_bkp_retained_clear(const struct device *dev)
{
	const struct stm32_bkp_retained_config *config = dev->config;

	stm32_backup_domain_enable_access();

	for (size_t i = 0; i < (config->size / 4U); i++) {
		*stm32_bkp_reg(config, i) = 0U;
	}

	stm32_backup_domain_disable_access();

	return 0;
}

static DEVICE_API(retained_mem, stm32_bkp_retained_api) = {
	.size = stm32_bkp_retained_size,
	.read = stm32_bkp_retained_read,
	.write = stm32_bkp_retained_write,
	.clear = stm32_bkp_retained_clear,
};

#define STM32_BKP_RETAINED_DEVICE(inst)                                                            \
	BUILD_ASSERT((DT_INST_PROP(inst, size) % 4) == 0, "size must be a multiple of 4");         \
	static const struct stm32_bkp_retained_config stm32_bkp_retained_config_##inst = {         \
		.first = DT_INST_PROP(inst, first_register),                                       \
		.size = DT_INST_PROP(inst, size),                                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, &stm32_bkp_retained_config_##inst,           \
			      POST_KERNEL, CONFIG_RETAINED_MEM_INIT_PRIORITY,                      \
			      &stm32_bkp_retained_api);

DT_INST_FOREACH_STATUS_OKAY(STM32_BKP_RETAINED_DEVICE)
