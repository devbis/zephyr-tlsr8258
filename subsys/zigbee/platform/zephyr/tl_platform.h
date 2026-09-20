/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zephyr-compatible replacement for tl_zigbee_sdk/proj/platform.h.
 * Declares MCU_CORE_8258 so SDK source files that check this macro work.
 */
#pragma once

#define MCU_CORE_8258   1

#include "compiler_zephyr.h"
#include <zephyr/zigbee/zb_types.h>
