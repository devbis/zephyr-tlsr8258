/* SPDX-License-Identifier: Apache-2.0 */
/* Host stand-in for <zephyr/sys/util.h>. */
#ifndef ZIGBEE_HOST_SYS_UTIL_H
#define ZIGBEE_HOST_SYS_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define BIT(n)		(1UL << (n))
#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)	(((a) < (b)) ? (a) : (b))
#define MAX(a, b)	(((a) > (b)) ? (a) : (b))

#endif /* ZIGBEE_HOST_SYS_UTIL_H */
