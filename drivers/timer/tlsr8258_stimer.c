/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_stimer

#include <zephyr/device.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <tlsr825x/irq.h>

#define TLSR8258_REG_SYSTEM_TICK       (*(volatile uint32_t *)0x00800740u)
#define TLSR8258_REG_SYSTEM_TICK_IRQ   (*(volatile uint32_t *)0x00800744u)
#define TLSR8258_REG_SYSTEM_TICK_MODE  (*(volatile uint8_t *)0x0080074cu)
#define TLSR8258_REG_SYSTEM_TICK_CTRL  (*(volatile uint8_t *)0x0080074fu)

#define FLD_SYSTEM_TICK_START BIT(0)
#define FLD_SYSTEM_TICK_IRQ_EN BIT(1)
#define FLD_SYSTEM_TICK_16M BIT(4)

#define CYCLES_PER_TICK (sys_clock_hw_cycles_per_sec() / CONFIG_SYS_CLOCK_TICKS_PER_SEC)

__weak void tlsr8258_stimer_debug_tick(void)
{
}

static void tlsr8258_stimer_irq(const void *unused)
{
	ARG_UNUSED(unused);

	*TLSR8258_REG_IRQ_SRC = BIT(TLSR8258_IRQ_SYSTEM_TIMER);
#if defined(CONFIG_TLSR8258_STIMER_DEBUG_HOOK)
	tlsr8258_stimer_debug_tick();
#endif
	TLSR8258_REG_SYSTEM_TICK_IRQ = (TLSR8258_REG_SYSTEM_TICK_IRQ + CYCLES_PER_TICK) & ~0x7u;
	sys_clock_announce(1);
}

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	ARG_UNUSED(ticks);
	ARG_UNUSED(idle);

	uint32_t key = irq_lock();

	*TLSR8258_REG_IRQ_SRC = BIT(TLSR8258_IRQ_SYSTEM_TIMER);
	TLSR8258_REG_SYSTEM_TICK_IRQ = (TLSR8258_REG_SYSTEM_TICK + CYCLES_PER_TICK) & ~0x7u;
	TLSR8258_REG_SYSTEM_TICK_MODE |= FLD_SYSTEM_TICK_IRQ_EN;
	*TLSR8258_REG_IRQ_MASK |= BIT(TLSR8258_IRQ_SYSTEM_TIMER);

	irq_unlock(key);
}

uint32_t sys_clock_cycle_get_32(void)
{
	return TLSR8258_REG_SYSTEM_TICK;
}

uint32_t sys_clock_elapsed(void)
{
	return 0;
}

static int tlsr8258_stimer_init(void)
{
	IRQ_CONNECT(TLSR8258_IRQ_SYSTEM_TIMER, 0, tlsr8258_stimer_irq, NULL, 0);

	*TLSR8258_REG_IRQ_MASK &= ~BIT(TLSR8258_IRQ_SYSTEM_TIMER);
	*TLSR8258_REG_IRQ_SRC = BIT(TLSR8258_IRQ_SYSTEM_TIMER);
	TLSR8258_REG_SYSTEM_TICK = 0u;
	TLSR8258_REG_SYSTEM_TICK_MODE = 0u;
	TLSR8258_REG_SYSTEM_TICK_MODE = FLD_SYSTEM_TICK_16M;
	TLSR8258_REG_SYSTEM_TICK_CTRL = FLD_SYSTEM_TICK_START;

	return 0;
}

SYS_INIT(tlsr8258_stimer_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
