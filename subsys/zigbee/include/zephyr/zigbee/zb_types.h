/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/* Based on Telink tl_zigbee_sdk/proj/common/types.h (Apache 2.0) */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SOC_FAMILY_STM32
/*
 * The STM32 CMSIS device header declares enumerators SUCCESS and ERROR; it
 * must be seen before the macros below.
 */
#include <soc.h>
#endif

typedef uint8_t  status_t;
typedef uint32_t UTCTime;
typedef uint32_t arg_t;
typedef void (*fn_ptr)(uint8_t *);

enum { ZB_FALSE = 0, ZB_TRUE = 1 };

typedef uint8_t cId_t;
typedef uint8_t addrExt_t[8];
typedef uint8_t extPANId_t[8];
typedef addrExt_t extAddr_t;

typedef union {
	uint16_t shortAddr;
	addrExt_t extAddr;
} zb_addr_t;

typedef union {
	uint32_t srcId;
	addrExt_t gpdIeeeAddr;
} gpdId_t;

#define ADDR_MODE_NONE   0
#define ADDR_MODE_SHORT  2
#define ADDR_MODE_EXT    3

typedef struct {
	union {
		uint16_t shortAddr;
		addrExt_t extAddr;
	} addr;
	uint8_t addrMode;
} addr_t;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE  (!FALSE)
#endif

#define U32_MAX_VAL  ((uint32_t)0xffffffff)
#define U16_MAX_VAL  ((uint16_t)0xffff)
#define U8_MAX_VAL   ((uint8_t)0xff)

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
