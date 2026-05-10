/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/sys/printk.h>
#include <tlsr825x/irq.h>
#include <tlsr825x/power.h>

volatile uint32_t __noinit tlsr_pm_marker;

static void park(uint32_t marker)
{
	tlsr_pm_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

int main(void)
{
	int ret;
	enum tlsr8258_pm_wakeup_reason reason;

	tlsr_pm_marker = 0x8258aa01u;
	printk("tlsr8258 pm timer smoke\n");
	/*
	 * Raw stall suspend uses TMR1 wakeup and does not synchronize Zephyr's
	 * system timer accounting. Keep the system timer IRQ masked for this
	 * board-local smoke path so post-wake scheduler activity cannot preempt
	 * the direct success/fail markers.
	 */
	*TLSR8258_REG_IRQ_MASK &= ~BIT(TLSR8258_IRQ_SYSTEM_TIMER);
	tlsr8258_irq_clear_parent(TLSR8258_IRQ_SYSTEM_TIMER);

	ret = tlsr8258_pm_suspend_for_ms(100u);
	if (ret != 0) {
		park(0x8258e101u);
	}

	reason = tlsr8258_pm_get_wakeup_reason();
	if (reason != TLSR8258_PM_WAKEUP_TIMER) {
		park(0x8258e102u);
	}

	park(0x8258aa00u);
}
