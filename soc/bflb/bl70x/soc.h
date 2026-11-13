/*
 * Copyright (c) 2021-2025 ATL Electronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Board configuration macros
 *
 * This header file is used to specify and describe board-level aspects
 */

#ifndef _SOC__H_
#define _SOC__H_

#include <zephyr/sys/util.h>

#ifndef _ASMLANGUAGE

/* Add include for DTS generated information */
#include <zephyr/devicetree.h>
/* Add include for soc function attributes */
#include "soc_function_attributes.h"

/** What ended the hibernate (PM_STATE_SOFT_OFF) the firmware was started from. */
enum bl70x_wakeup_cause {
	/** The start is not a wakeup: power-up or a reset. */
	BL70X_WAKEUP_NONE,
	/** The RTC reached the time of the next kernel timeout. */
	BL70X_WAKEUP_TIMER,
	/** A key of a gpio-keys node with wakeup-source was pressed. */
	BL70X_WAKEUP_PIN,
};

/**
 * @brief Get the cause of the wakeup from the hibernate.
 *
 * @retval BL70X_WAKEUP_NONE The firmware was not started by a wakeup
 * @retval BL70X_WAKEUP_TIMER The RTC ended the hibernate
 * @retval BL70X_WAKEUP_PIN A wakeup key ended the hibernate
 */
enum bl70x_wakeup_cause bl70x_wakeup_cause(void);

#endif /* !_ASMLANGUAGE */

#endif /* _SOC__H_ */
