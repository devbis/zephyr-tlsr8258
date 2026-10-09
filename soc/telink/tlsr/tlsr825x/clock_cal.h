/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_TELINK_TLSR825X_CLOCK_CAL_H_
#define SOC_TELINK_TLSR825X_CLOCK_CAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Analog register accessors (ana_xx space behind 0x8000b8..0x8000ba). */
uint8_t tlsr8258_analog_read(uint8_t addr);
void tlsr8258_analog_write(uint8_t addr, uint8_t value);

/* Select the internal 32 kHz RC oscillator as the 32 kHz clock source. */
void tlsr8258_clock_32k_rc_init(void);

/* Calibrate the 32 kHz RC oscillator (vendor rc_32k_cal()). */
void tlsr8258_rc_32k_cal(void);

/* Calibrate the 24 MHz RC oscillator (vendor rc_24m_cal()). */
void tlsr8258_rc_24m_cal(void);

/* Calibrate the 48 MHz RC oscillator (vendor rc_48m_cal()). */
void tlsr8258_rc_48m_cal(void);

/*
 * Calibrate the doubler clock (vendor doubler_calibration()). Runs from RAM:
 * the caller must not use flash while it executes.
 */
void tlsr8258_doubler_calibration(void);

#ifdef __cplusplus
}
#endif

#endif /* SOC_TELINK_TLSR825X_CLOCK_CAL_H_ */
