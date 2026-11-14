/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file SoC configuration macros for the STM32WB family processors.
 *
 * Based on reference manual:
 *   TODO: Provide reference when known
 *
 * Chapter 2.2.2: Memory map and register boundary addresses
 */


#ifndef _STM32WBX_SOC_H_
#define _STM32WBX_SOC_H_

#ifndef _ASMLANGUAGE

#include <stm32wbxx.h>

enum stm32wb_standby_wake {
	/** The reset was not the end of a PM_STATE_SOFT_OFF sleep. */
	STM32WB_STANDBY_WAKE_NONE,
	/** The system left Standby on the RTC wakeup timer. */
	STM32WB_STANDBY_WAKE_STANDBY,
	/** The system left Standby by a reset from the NRST pin (RESET button). */
	STM32WB_STANDBY_WAKE_RESET_PIN,
};

/** How the last reset related to a PM_STATE_SOFT_OFF sleep. */
enum stm32wb_standby_wake stm32wb_standby_wake_get(void);

#endif /* !_ASMLANGUAGE */

#endif /* _STM32WBX_SOC_H_ */
