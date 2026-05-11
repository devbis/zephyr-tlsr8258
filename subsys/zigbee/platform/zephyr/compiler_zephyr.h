/* SPDX-License-Identifier: Apache-2.0 */
/* Based on Telink tl_zigbee_sdk/proj/common/compiler.h (Apache 2.0) */
#pragma once

#include <zephyr/toolchain.h>

#define _attribute_packed_          __packed
#define _attribute_aligned_(n)      __aligned(n)
#define _attribute_session_(s)      /* no-op on Zephyr */
#define _attribute_ram_code_        /* no-op: Zephyr handles RAMFUNC via linker */
#define _attribute_no_inline_       __attribute__((noinline))
#define _attribute_always_inline_   __attribute__((always_inline)) static inline
#define _attribute_data_retention_  /* no-op */
#define _attribute_custom_code_     /* no-op */
#define _attribute_gpio_wakeup_     /* no-op */

/* Some vendor headers reference u_int8_t */
#ifndef u_int8_t
typedef unsigned char u_int8_t;
#endif
