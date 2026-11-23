/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Command IDs, status codes and the parameter reader and writer of the EZSP commands (Silicon
 * Labs UG100, protocol version 13). Parameters are little endian.
 */

/* Command and callback IDs. */
#define EZSP_ID_ADD_ENDPOINT              0x0002U
#define EZSP_ID_NOP                       0x0005U
#define EZSP_ID_SET_MANUFACTURER_CODE     0x0015U
#define EZSP_ID_GET_EXTENDED_VALUE        0x0003U
#define EZSP_ID_GET_CONFIGURATION_VALUE   0x0052U
#define EZSP_ID_SET_CONFIGURATION_VALUE   0x0053U
#define EZSP_ID_SET_POLICY                0x0055U
#define EZSP_ID_GET_POLICY                0x0056U
#define EZSP_ID_GET_MFG_TOKEN             0x000BU
#define EZSP_ID_SET_CONCENTRATOR          0x0010U
#define EZSP_ID_NETWORK_INIT              0x0017U
#define EZSP_ID_NETWORK_STATE             0x0018U
#define EZSP_ID_STACK_STATUS_HANDLER      0x0019U
#define EZSP_ID_GET_EUI64                 0x0026U
#define EZSP_ID_GET_NODE_ID               0x0027U
#define EZSP_ID_GET_NETWORK_PARAMETERS    0x0028U
#define EZSP_ID_SET_SOURCE_ROUTE_DISC     0x005AU
#define EZSP_ID_GET_CURRENT_SECURITY_STATE 0x0069U
#define EZSP_ID_SET_RADIO_POWER           0x0099U
#define EZSP_ID_CLEAR_KEY_TABLE           0x00B1U
#define EZSP_ID_EXPORT_KEY                0x0114U
#define EZSP_ID_GET_NETWORK_KEY_INFO      0x0116U
#define EZSP_ID_ECHO                      0x0081U
#define EZSP_ID_GET_VALUE                 0x00AAU
#define EZSP_ID_SET_VALUE                 0x00ABU

/* EzspStatus. */
#define EZSP_SUCCESS                      0x00U
#define EZSP_ERROR_INVALID_FRAME_ID       0x31U
#define EZSP_ERROR_TRUNCATED              0x33U
#define EZSP_ERROR_OVERFLOW               0x34U
#define EZSP_ERROR_OUT_OF_MEMORY          0x35U
#define EZSP_ERROR_INVALID_VALUE          0x36U
#define EZSP_ERROR_INVALID_ID             0x37U
#define EZSP_ERROR_INVALID_CALL           0x38U

/* EmberStatus, as the commands before the security manager return it. */
#define EMBER_SUCCESS                     0x00U
#define EMBER_BAD_ARGUMENT                0x02U
#define EMBER_NOT_JOINED                  0x93U
#define EMBER_NETWORK_UP                  0x90U
#define EMBER_NETWORK_DOWN                0x91U
#define EMBER_INVALID_CALL                0x70U

/* sl_Status, as the security manager commands return it. */
#define SL_STATUS_OK                      0x00000000UL
#define SL_STATUS_INVALID_PARAMETER       0x00000021UL
#define SL_STATUS_NOT_FOUND               0x0000000CUL

/* EmberNetworkStatus and EmberNodeType. */
#define EMBER_NO_NETWORK                  0U
#define EMBER_JOINED_NETWORK              2U
#define EMBER_COORDINATOR                 1U
#define EMBER_UNKNOWN_DEVICE              0U

/* Reader of the parameters of a command. A read past the end sets err and returns zeros. */
struct ezsp_rd {
	const uint8_t *p;
	size_t left;
	bool err;
};

static inline void ezsp_rd_init(struct ezsp_rd *r, const uint8_t *p, size_t len)
{
	r->p = p;
	r->left = len;
	r->err = false;
}

static inline uint8_t ezsp_rd_u8(struct ezsp_rd *r)
{
	uint8_t v;

	if (r->left < 1U) {
		r->err = true;
		return 0U;
	}
	v = r->p[0];
	r->p++;
	r->left--;

	return v;
}

static inline uint16_t ezsp_rd_u16(struct ezsp_rd *r)
{
	uint16_t lo = ezsp_rd_u8(r);
	uint16_t hi = ezsp_rd_u8(r);

	return (uint16_t)(lo | (hi << 8));
}

static inline uint32_t ezsp_rd_u32(struct ezsp_rd *r)
{
	uint32_t lo = ezsp_rd_u16(r);
	uint32_t hi = ezsp_rd_u16(r);

	return lo | (hi << 16);
}

/* A pointer to @p len bytes, or NULL (and err) when fewer are left. */
static inline const uint8_t *ezsp_rd_bytes(struct ezsp_rd *r, size_t len)
{
	const uint8_t *p = r->p;

	if (r->left < len) {
		r->err = true;
		return NULL;
	}
	r->p += len;
	r->left -= len;

	return p;
}

/* Writer of the parameters of a response. A write past the end sets err and writes nothing. */
struct ezsp_wr {
	uint8_t *p;
	size_t size;
	size_t len;
	bool err;
};

static inline void ezsp_wr_init(struct ezsp_wr *w, uint8_t *p, size_t size)
{
	w->p = p;
	w->size = size;
	w->len = 0U;
	w->err = false;
}

static inline void ezsp_wr_u8(struct ezsp_wr *w, uint8_t v)
{
	if (w->len + 1U > w->size) {
		w->err = true;
		return;
	}
	w->p[w->len++] = v;
}

static inline void ezsp_wr_u16(struct ezsp_wr *w, uint16_t v)
{
	ezsp_wr_u8(w, (uint8_t)(v & 0xFFU));
	ezsp_wr_u8(w, (uint8_t)(v >> 8));
}

static inline void ezsp_wr_u32(struct ezsp_wr *w, uint32_t v)
{
	ezsp_wr_u16(w, (uint16_t)(v & 0xFFFFU));
	ezsp_wr_u16(w, (uint16_t)(v >> 16));
}

static inline void ezsp_wr_bytes(struct ezsp_wr *w, const uint8_t *b, size_t len)
{
	if (w->len + len > w->size) {
		w->err = true;
		return;
	}
	if (len > 0U) {
		memcpy(&w->p[w->len], b, len);
		w->len += len;
	}
}
