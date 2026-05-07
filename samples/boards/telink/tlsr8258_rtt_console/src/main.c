/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>

volatile uint32_t tlsr8258_rtt_marker;
volatile uint32_t tlsr8258_rtt_count;

int main(void)
{
	tlsr8258_rtt_marker = 0x8258a000u;

	printk("tlsr8258 RTT console start\n");

	for (;;) {
		tlsr8258_rtt_count++;
		tlsr8258_rtt_marker = 0x8258a000u | (tlsr8258_rtt_count & 0xffu);

		printk("printk tick\n");

		k_sleep(K_MSEC(1000));
	}
}
