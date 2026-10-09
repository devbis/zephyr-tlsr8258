/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_ADC_TLSR8258_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_ADC_TLSR8258_H_

#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the ADC voltage reference used to convert codes to millivolts.
 *
 * The TLSR8258 SAR ADC is trimmed per chip; the calibration values are read
 * from the factory area (vendor drv_calibration()). Until this is called the
 * driver uses 1175 mV with a 0 mV offset.
 *
 * @param dev ADC device.
 * @param vref_mv Reference voltage in millivolts.
 * @param offset_mv Offset in millivolts added to every conversion.
 *
 * @retval 0 Calibration stored.
 * @retval -EINVAL Reference voltage is zero.
 */
int adc_tlsr8258_set_calibration(const struct device *dev, uint16_t vref_mv, int8_t offset_mv);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_ADC_TLSR8258_H_ */
