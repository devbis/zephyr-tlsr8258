/* SPDX-License-Identifier: Apache-2.0 */
/* Host stand-in for <zephyr/kernel.h>: the Zigbee headers embed a few kernel
 * objects by value, so give them a size without pulling in the kernel.
 */
#ifndef ZIGBEE_HOST_KERNEL_H
#define ZIGBEE_HOST_KERNEL_H

#include <stdint.h>

struct k_work_delayable {
	void *reserved[8];
};

struct k_timer {
	void *reserved[8];
};

struct k_sem {
	void *reserved[4];
};

struct k_mutex {
	void *reserved[4];
};

void k_busy_wait(uint32_t usec);

#endif /* ZIGBEE_HOST_KERNEL_H */
