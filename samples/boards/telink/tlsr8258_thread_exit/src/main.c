/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>

volatile uint32_t tlsr_thread_exit_count;
volatile uint32_t tlsr_thread_exit_marker;

K_THREAD_STACK_DEFINE(worker_stack, 1024);
static struct k_thread worker_thread;

static FUNC_NORETURN void park_with_marker(uint32_t marker)
{
	tlsr_thread_exit_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

static void worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	tlsr_thread_exit_marker = 0x82581001u;
	k_sleep(K_MSEC(20));
	tlsr_thread_exit_count++;
	tlsr_thread_exit_marker = 0x82581002u;
}

int main(void)
{
	tlsr_thread_exit_marker = 0x82581000u;

	k_thread_create(&worker_thread, worker_stack,
			K_THREAD_STACK_SIZEOF(worker_stack),
			worker, NULL, NULL, NULL,
			K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

	k_sleep(K_MSEC(100));

	if (tlsr_thread_exit_count != 1u) {
		park_with_marker(0x8258e101u);
	}

	if (tlsr_thread_exit_marker != 0x82581002u) {
		park_with_marker(0x8258e102u);
	}

	park_with_marker(0x82580000u);
}
