/* SPDX-License-Identifier: Apache-2.0 */
/* Based on Telink tl_zigbee_sdk/proj/common/types.h (Apache 2.0) */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/sys/util.h>

typedef uint8_t   u8;
typedef int8_t    s8;
typedef uint16_t  u16;
typedef int16_t   s16;
typedef uint32_t  u32;
typedef int32_t   s32;
typedef uint64_t  u64;
typedef int64_t   s64;

typedef u32 u24;
typedef s32 s24;
typedef u8  status_t;
typedef u32 UTCTime;
typedef u32 arg_t;
typedef void (*fn_ptr)(u8 *);

enum { ZB_FALSE = 0, ZB_TRUE = 1 };

typedef u8 cId_t;
typedef u8 addrExt_t[8];
typedef u8 extPANId_t[8];
typedef addrExt_t extAddr_t;

typedef union {
	u16 shortAddr;
	addrExt_t extAddr;
} tl_zb_addr_t;

typedef union {
	u32 srcId;
	addrExt_t gpdIeeeAddr;
} gpdId_t;

#define ADDR_MODE_NONE   0
#define ADDR_MODE_SHORT  2
#define ADDR_MODE_EXT    3

typedef struct {
	union {
		u16 shortAddr;
		addrExt_t extAddr;
	} addr;
	u8 addrMode;
} addr_t;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE  (!FALSE)
#endif

#define U32_MAX_VAL  ((u32)0xffffffff)
#define U16_MAX_VAL  ((u16)0xffff)
#define U8_MAX_VAL   ((u8)0xff)

/* Generic status codes (match SDK values) */
#define SUCCESS                 0x00
#define FAILURE                 0x01
#define INVALIDPARAMETER        0x02
#define NO_TIMER_AVAIL          0x08
#define NV_ITEM_UNINIT          0x09
#define NV_OPER_FAILED          0x0A

#ifndef OFFSETOF
#define OFFSETOF(type, member) offsetof(type, member)
#endif
