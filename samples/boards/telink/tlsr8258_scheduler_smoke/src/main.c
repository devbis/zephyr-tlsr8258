/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>

volatile uint32_t tlsr_sched_marker;
volatile uint32_t tlsr_sched_thread_a_count;
volatile uint32_t tlsr_sched_thread_b_count;
volatile uint32_t tlsr_sched_sleeper_count;
volatile uint32_t tlsr_sched_abort_count;
volatile int32_t tlsr_sched_join_a_ret;
volatile int32_t tlsr_sched_join_b_ret;

K_THREAD_STACK_DEFINE(thread_a_stack, 1024);
K_THREAD_STACK_DEFINE(thread_b_stack, 1024);
K_THREAD_STACK_DEFINE(sleeper_stack, 1024);

static struct k_thread thread_a;
static struct k_thread thread_b;
static struct k_thread sleeper_thread;

static FUNC_NORETURN void park_with_marker(uint32_t marker)
{
	tlsr_sched_marker = marker;

	for (;;) {
		compiler_barrier();
	}
}

static void yield_worker(void *p1, void *p2, void *p3)
{
	volatile uint32_t *counter = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (uint32_t i = 0u; i < 16u; i++) {
		(*counter)++;
		k_yield();
	}
}

static void sleep_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		tlsr_sched_sleeper_count++;
		k_sleep(K_MSEC(5));
	}
}

int main(void)
{
	uint32_t before_abort;

	tlsr_sched_marker = 0x82584000u;

	k_thread_create(&thread_a, thread_a_stack, K_THREAD_STACK_SIZEOF(thread_a_stack),
			yield_worker, (void *)&tlsr_sched_thread_a_count, NULL, NULL,
			K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	k_thread_create(&thread_b, thread_b_stack, K_THREAD_STACK_SIZEOF(thread_b_stack),
			yield_worker, (void *)&tlsr_sched_thread_b_count, NULL, NULL,
			K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

	tlsr_sched_join_a_ret = k_thread_join(&thread_a, K_MSEC(250));
	tlsr_sched_join_b_ret = k_thread_join(&thread_b, K_MSEC(250));

	if (tlsr_sched_join_a_ret != 0 || tlsr_sched_join_b_ret != 0) {
		park_with_marker(0x8258e401u);
	}

	if (tlsr_sched_thread_a_count != 16u || tlsr_sched_thread_b_count != 16u) {
		park_with_marker(0x8258e402u);
	}

	k_thread_create(&sleeper_thread, sleeper_stack, K_THREAD_STACK_SIZEOF(sleeper_stack),
			sleep_worker, NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

	k_sleep(K_MSEC(20));
	before_abort = tlsr_sched_sleeper_count;
	k_thread_abort(&sleeper_thread);
	tlsr_sched_abort_count++;
	k_sleep(K_MSEC(20));

	if (before_abort == 0u || tlsr_sched_sleeper_count != before_abort) {
		park_with_marker(0x8258e403u);
	}

	park_with_marker(0x82580000u);
}
