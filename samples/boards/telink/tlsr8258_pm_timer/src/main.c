/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <tlsr825x/irq.h>
#include <tlsr825x/power.h>

int main(void)
{
	enum tlsr8258_pm_wakeup_reason reason;
	int ret;

	printk("TLSR8258 suspend with timer wakeup\n");

	/*
	 * The suspend stalls the core and wakes on the 32 kHz timer without
	 * advancing the kernel clock, so keep the system timer masked across it.
	 */
	tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() & ~BIT(TLSR8258_IRQ_SYSTEM_TIMER));
	tlsr8258_irq_clear_parent(TLSR8258_IRQ_SYSTEM_TIMER);

	ret = tlsr8258_pm_suspend_for_ms(100u);
	reason = tlsr8258_pm_get_wakeup_reason();

	tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() | BIT(TLSR8258_IRQ_SYSTEM_TIMER));

	if (ret != 0) {
		printk("suspend failed: %d\n", ret);
		return 0;
	}

	printk("woke up, reason %s\n", (reason == TLSR8258_PM_WAKEUP_TIMER) ? "timer" : "other");

	return 0;
}
