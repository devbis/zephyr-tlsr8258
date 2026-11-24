/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/linker/sections.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "soc.h"

/*
 * The deep sleep follows hal_pwrmgr_enter_sleep_rtc_reset() of the PHY62XX SDK 3.1.1: configure the
 * RTC, put the chip to sleep through the ROM and let it start from the boot ROM when it wakes. The
 * SDK clears the RAM retention there; this keeps the blocks that hold the zephyr,retained-ram
 * nodes, and arms the wake pads of the GPIO interrupts as well. The sleep flag of the ROM stays
 * clear, so the ROM boots normally and does not call the wakeup function of the jump table.
 */
#define ROM_CONFIG_RTC ((void (*)(uint32_t))0x0000a6f9U)
#define ROM_ENTER_SLEEP_OFF_MODE ((void (*)(uint32_t))0x0000afa1U)
#define SYSTEM_SLEEP_MODE 1U

#define AON_PMCTL2_0 0x4000f01cU
#define AON_SLEEP_R0 0x4000f0c0U
#define PMCTL2_0_SW_CONTROL BIT(6)
#define PMCTL2_0_RETENTION_MASK GENMASK(21, 17)
#define PMCTL2_0_RETENTION_SHIFT 17U

/* The RC oscillator of the RTC runs at 32768 Hz */
#define RTC_HZ 32768U
/*
 * The value of AON->SLEEP_R[0] that the SDK writes for this sleep: the boot ROM makes the cause 5, a
 * warm start of the user mode, of it and skips the window of its loader.
 */
#define SLEEP_R0_USER_WARM_START 4U

/*
 * The blocks of the SRAM that can be retained, bits 0 to 2 of the retention field: 32 KiB at
 * 0x1fff0000, 16 KiB at 0x1fff8000 and 16 KiB at 0x1fffc000.
 */
#define SRAM_BLOCK(address) ((address) < 0x1fff8000U ? 0U : (address) < 0x1fffc000U ? 1U : 2U)
/* The zephyr,retained-ram node is the child of the memory region that has the reg */
#define RETAINED_BLOCKS(node_id)                                                                   \
	(GENMASK(SRAM_BLOCK(DT_REG_ADDR(DT_PARENT(node_id)) + DT_REG_SIZE(DT_PARENT(node_id)) - 1U), \
		 SRAM_BLOCK(DT_REG_ADDR(DT_PARENT(node_id))))) |

#define RETAINED_RAM_BLOCKS (DT_FOREACH_STATUS_OKAY(zephyr_retained_ram, RETAINED_BLOCKS) 0U)

static __ramfunc void phy62x2_sleep_enter(uint32_t rtc_ticks)
{
	volatile uint32_t *const pmctl2_0 = (volatile uint32_t *)AON_PMCTL2_0;

	/* Disable the software control of the power, as the SDK does */
	*pmctl2_0 &= ~PMCTL2_0_SW_CONTROL;

	if (rtc_ticks != 0U) {
		ROM_CONFIG_RTC(rtc_ticks);
	}

	/* The retention of the SRAM blocks, then the reset cause for the boot ROM */
	*pmctl2_0 = (*pmctl2_0 & ~PMCTL2_0_RETENTION_MASK) |
		    (RETAINED_RAM_BLOCKS << PMCTL2_0_RETENTION_SHIFT);
	*(volatile uint32_t *)AON_SLEEP_R0 = SLEEP_R0_USER_WARM_START;

	ROM_ENTER_SLEEP_OFF_MODE(SYSTEM_SLEEP_MODE);

	for (;;) {
	}
}

void phy62x2_deep_sleep(uint32_t timeout_ms)
{
	(void)irq_lock();

	phy62x2_wakeup_arm(true);
	phy62x2_sleep_enter((uint32_t)(((uint64_t)timeout_ms * RTC_HZ) / 1000U));

	CODE_UNREACHABLE;
}
