/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_STUB_ZB_TYPES_H_
#define TEST_STUB_ZB_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

#ifndef _attribute_packed_
#define _attribute_packed_ __attribute__((packed))
#endif

#endif
