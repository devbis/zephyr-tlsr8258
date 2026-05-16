/* SPDX-License-Identifier: Apache-2.0 */
/* Based on Telink tl_zigbee_sdk/proj/common/compiler.h (Apache 2.0) */
#pragma once

#include <zephyr/toolchain.h>

#define _attribute_packed_          __packed
#define _attribute_aligned_(n)      __aligned(n)
#define _attribute_session_(s)      /* no-op on Zephyr */
#define _attribute_ram_code_        __ramfunc
#define _attribute_no_inline_       __attribute__((noinline))
#define _attribute_always_inline_   __attribute__((always_inline)) static inline
#define _attribute_data_retention_  /* no-op */
#define _attribute_custom_code_     /* no-op */
#define _attribute_gpio_wakeup_     /* no-op */

/* SDK code-section attributes — no-op on Zephyr (linker script handles placement) */
#define _CODE_APS_
#define _CODE_BDB_
#define _CODE_ZCL_
#define _CODE_NWK_
#define _CODE_ZDO_
#define _CODE_AF_
#define _CODE_SS_
#define _CODE_GP_
#define _CODE_MAC_

/* Some vendor headers reference u_int8_t */
#ifndef u_int8_t
typedef unsigned char u_int8_t;
#endif
