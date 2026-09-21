/* SPDX-License-Identifier: Apache-2.0 */
/* Host stand-in for <zephyr/random/random.h>. */
#ifndef ZIGBEE_HOST_RANDOM_H
#define ZIGBEE_HOST_RANDOM_H

#include <stdint.h>

static inline uint32_t sys_rand32_get(void)
{
	return 0U;
}

#endif /* ZIGBEE_HOST_RANDOM_H */
