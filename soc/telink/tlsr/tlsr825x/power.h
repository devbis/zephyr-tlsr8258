/*
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

int tlsr8258_pm_suspend_for_ms(uint32_t duration_ms);
int tlsr8258_pm_deep_retention_for_ms(uint32_t duration_ms);
int tlsr8258_pm_shutdown_for_ms(uint32_t duration_ms);
int tlsr8258_pm_configure_gpio_wakeup(uint8_t port, uint8_t pin, bool active_low, bool enable);
enum tlsr8258_pm_wakeup_reason tlsr8258_pm_get_wakeup_reason(void);
uint32_t tlsr8258_pm_get_wakeup_raw_status(void);

#ifdef __cplusplus
}
#endif

#endif /* SOC_TELINK_TLSR825X_POWER_H_ */
