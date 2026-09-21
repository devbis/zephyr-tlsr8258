/* SPDX-License-Identifier: Apache-2.0 */
/* Host stand-in for <zephyr/toolchain.h>: enough for the Zigbee headers. */
#ifndef ZIGBEE_HOST_TOOLCHAIN_H
#define ZIGBEE_HOST_TOOLCHAIN_H

#include <stddef.h>

#define __packed	__attribute__((packed))
#ifndef __used
#define __used		__attribute__((used))
#endif
#define __aligned(x)	__attribute__((aligned(x)))
#ifndef __unused
#define __unused	__attribute__((unused))
#endif

#endif /* ZIGBEE_HOST_TOOLCHAIN_H */
