/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/random/random.h>
#include <zephyr/zigbee/zb_types.h>
#include "drv_hw.h"

/* 24 MHz system clock → 24 ticks per microsecond */
u32 sysTimerPerUs = 24;

startup_state_e drv_platform_init(void)
{
	return SYSTEM_BOOT;
}

void drv_enable_irq(void)
{
	irq_unlock(0);
}

u32 drv_disable_irq(void)
{
	return irq_lock();
}

u32 drv_restore_irq(u32 en)
{
	irq_unlock(en);
	return en;
}

void drv_irqMask_clear(void)
{
}

void drv_wd_setInterval(u32 ms)
{
	ARG_UNUSED(ms);
}

void drv_wd_start(void)
{
}

void drv_wd_clear(void)
{
}

u32 drv_u32Rand(void)
{
	u32 val;

	sys_rand_get(&val, sizeof(val));
	return val;
}

void drv_generateRandomData(u8 *pData, u8 len)
{
	sys_rand_get(pData, len);
}

void voltage_detect(bool powerOn)
{
	ARG_UNUSED(powerOn);
}

void drv_vbusWatchdogClose(void)
{
}
