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

/*
 * Vendor sleep-mode byte (written to analog reg 0x7e). Only SUSPEND and DEEP
 * are implemented today -- both return via the normal suspend/wake sequence
 * (SUSPEND resumes in place; DEEP always forces a full chip reboot on wake,
 * see tlsr8258_pm_deep_sleep_for_ms()). The DEEPSLEEP_MODE_RET_SRAM_* variants
 * exist in the vendor SDK and the tlsr82xx-hal Rust port
 * (../../../tlsr82xx/tlsr82xx-hal/src/pm.rs) but are NOT implemented here:
 * unlike SUSPEND/DEEP, resuming from a retention sleep re-enters the reset
 * vector rather than returning from a function call (the CPU digital domain
 * loses power; only the low 32K of SRAM stays retained), which needs a
 * warm-resume mechanism (save SP + a resume point before sleeping, then at
 * the reset vector -- arch/tc32/core/reset.S's z_prep_c_flash_continue,
 * before the .Lbss_start zero loop -- detect a retention wake via analog
 * regs 0x7e/0x44/0x7f, skip the normal .bss/.data init since the retained
 * window already holds valid state, and jump back to the saved resume
 * point instead of falling through to z_cstart). That mechanism does not
 * exist in this port yet; add it as a follow-up before wiring
 * TLSR8258_PM_SLEEP_DEEP_RET_LOW32K through here and PM_STATE_STANDBY.
 */
enum tlsr8258_pm_sleep_mode {
	TLSR8258_PM_SLEEP_SUSPEND = 0x00,
	TLSR8258_PM_SLEEP_DEEP_RET_LOW32K = 0x07, /* not implemented, see above */
	TLSR8258_PM_SLEEP_DEEP = 0x80,
};

int tlsr8258_pm_suspend_for_ms(uint32_t duration_ms);
int tlsr8258_pm_deep_sleep_for_ms(uint32_t duration_ms);
int tlsr8258_pm_deep_retention_for_ms(uint32_t duration_ms);
int tlsr8258_pm_shutdown_for_ms(uint32_t duration_ms);
int tlsr8258_pm_configure_gpio_wakeup(uint8_t port, uint8_t pin, bool active_low, bool enable);
bool tlsr8258_pm_radio_can_suspend(void);
enum tlsr8258_pm_wakeup_reason tlsr8258_pm_get_wakeup_reason(void);
uint32_t tlsr8258_pm_get_wakeup_raw_status(void);

#ifdef __cplusplus
}
#endif

#endif /* SOC_TELINK_TLSR825X_POWER_H_ */
