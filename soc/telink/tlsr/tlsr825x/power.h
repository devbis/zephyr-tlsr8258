/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_TELINK_TLSR825X_POWER_H_
#define SOC_TELINK_TLSR825X_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum tlsr8258_pm_wakeup_reason {
	TLSR8258_PM_WAKEUP_NONE = 0,
	TLSR8258_PM_WAKEUP_TIMER = 1,
	TLSR8258_PM_WAKEUP_GPIO = 2,
	TLSR8258_PM_WAKEUP_CORE = 3,
	TLSR8258_PM_WAKEUP_COMPARATOR = 4,
	TLSR8258_PM_WAKEUP_UNKNOWN = 255,
};

enum tlsr8258_pm_gpio_port {
	TLSR8258_PM_GPIO_PORT_A = 0,
	TLSR8258_PM_GPIO_PORT_B = 1,
	TLSR8258_PM_GPIO_PORT_C = 2,
	TLSR8258_PM_GPIO_PORT_D = 3,
};

/*
 * Vendor sleep-mode byte (written to analog register 0x7e). SUSPEND resumes
 * in place. DEEP has no SRAM retention and wakes through a full reset. The
 * DEEP_RET_* modes wake through a full reset too (the firmware restarts from
 * the reset vector and initializes .data and .bss as on any boot) but keep the
 * low 8, 16 or 32 KiB of SRAM powered: the boot mirror at its start and, after
 * it, what is marked __tlsr8258_retained, which is not initialized or cleared
 * (see CONFIG_TLSR8258_PM_DEEP_RETENTION).
 */
enum tlsr8258_pm_sleep_mode {
	TLSR8258_PM_SLEEP_SUSPEND = 0x00,
	TLSR8258_PM_SLEEP_DEEP = 0x80,
	TLSR8258_PM_SLEEP_DEEP_RET_LOW8K = 0x61,
	TLSR8258_PM_SLEEP_DEEP_RET_LOW16K = 0x43,
	TLSR8258_PM_SLEEP_DEEP_RET_LOW32K = 0x07,
};

/*
 * Put a variable in the retained SRAM: its value survives a deep sleep. A cold
 * boot leaves it as the SRAM powered up (random), so a user keeps a validity
 * mark in it, with a checksum, and starts afresh when the mark is not right.
 * Needs CONFIG_TLSR8258_PM_DEEP_RETENTION; the size is CONFIG_TLSR8258_PM_RETENTION_SIZE.
 */
#define __tlsr8258_retained __attribute__((section(".tlsr8258_retained")))

int tlsr8258_pm_suspend_for_ms(uint32_t duration_ms);
int tlsr8258_pm_deep_sleep_for_ms(uint32_t duration_ms);
int tlsr8258_pm_configure_gpio_wakeup(uint8_t port, uint8_t pin, bool active_low, bool enable);

bool tlsr8258_pm_radio_can_suspend(void);
enum tlsr8258_pm_wakeup_reason tlsr8258_pm_get_wakeup_reason(void);
uint32_t tlsr8258_pm_get_wakeup_raw_status(void);

#ifdef __cplusplus
}
#endif

#endif /* SOC_TELINK_TLSR825X_POWER_H_ */
