/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_STUB_KERNEL_H_
#define TEST_STUB_KERNEL_H_

#include <stddef.h>
#include <stdint.h>

#define ARG_UNUSED(x) (void)(x)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define APPLICATION 0
#define SYS_INIT(fn, level, prio)

#endif
