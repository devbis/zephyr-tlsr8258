/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/irq_offload.h>

static irq_offload_routine_t offload_routine;
static const void *offload_param;

void arch_irq_offload(irq_offload_routine_t routine, const void *parameter)
{
	offload_routine = routine;
	offload_param = parameter;
	offload_routine(offload_param);
}
