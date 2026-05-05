/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>

#define TLSR8258_REG_TMR_CTRL (*(volatile uint32_t *)0x00800620u)
#define TLSR8258_FLD_TMR_WD_EN BIT(23)

void soc_prep_hook(void)
{
	/* Match vendor wd_stop()/WATCHDOG_DISABLE before C runtime setup. */
	TLSR8258_REG_TMR_CTRL &= ~TLSR8258_FLD_TMR_WD_EN;
}
