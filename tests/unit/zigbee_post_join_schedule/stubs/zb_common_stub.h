/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef void (*tl_zb_callback_t)(void *arg);

enum {
	RET_OK = 0,
	RET_ERROR,
	RET_BLOCKED,
	RET_EXIT,
	RET_BUSY,
	RET_EOF,
	RET_OUT_OF_RANGE,
	RET_EMPTY,
	RET_CANCELLED,
	RET_PENDING,
	RET_NO_MEMORY,
	RET_INVALID_PARAMETER,
	RET_OPERATION_FAILED,
	RET_BUFFER_TOO_SMALL,
	RET_END_OF_LIST,
	RET_ALREADY_EXISTS,
	RET_NOT_FOUND,
};

#define TL_SCHEDULE_TASK tl_zbTaskPost

u8 tl_zbTaskPost(tl_zb_callback_t fn, void *arg);
