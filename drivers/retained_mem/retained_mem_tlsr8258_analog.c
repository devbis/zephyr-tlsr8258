/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_analog_retained

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/sys/util.h>

#include <tlsr825x/clock_cal.h>

struct tlsr8258_retained_analog_config {
	uint8_t address;
	size_t size;
};

static ssize_t tlsr8258_retained_analog_size(const struct device *dev)
{
	const struct tlsr8258_retained_analog_config *config = dev->config;

	return (ssize_t)config->size;
}

static int tlsr8258_retained_analog_read(const struct device *dev, off_t offset,
					 uint8_t *buffer, size_t size)
{
	const struct tlsr8258_retained_analog_config *config = dev->config;

	for (size_t i = 0; i < size; i++) {
		buffer[i] = tlsr8258_analog_read((uint8_t)(config->address + offset + i));
	}

	return 0;
}

static int tlsr8258_retained_analog_write(const struct device *dev, off_t offset,
					  const uint8_t *buffer, size_t size)
{
	const struct tlsr8258_retained_analog_config *config = dev->config;

	for (size_t i = 0; i < size; i++) {
		tlsr8258_analog_write((uint8_t)(config->address + offset + i), buffer[i]);
	}

	return 0;
}

static int tlsr8258_retained_analog_clear(const struct device *dev)
{
	const struct tlsr8258_retained_analog_config *config = dev->config;

	for (size_t i = 0; i < config->size; i++) {
		tlsr8258_analog_write((uint8_t)(config->address + i), 0U);
	}

	return 0;
}

static DEVICE_API(retained_mem, tlsr8258_retained_analog_api) = {
	.size = tlsr8258_retained_analog_size,
	.read = tlsr8258_retained_analog_read,
	.write = tlsr8258_retained_analog_write,
	.clear = tlsr8258_retained_analog_clear,
};

#define TLSR8258_RETAINED_ANALOG_DEVICE(inst)                                                      \
	static const struct tlsr8258_retained_analog_config tlsr8258_retained_analog_config_##inst = { \
		.address = DT_INST_PROP(inst, analog_address),                                     \
		.size = DT_INST_PROP(inst, size),                                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, &tlsr8258_retained_analog_config_##inst,     \
			      POST_KERNEL, CONFIG_RETAINED_MEM_INIT_PRIORITY,                      \
			      &tlsr8258_retained_analog_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_RETAINED_ANALOG_DEVICE)
