/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_LE_H
#define _ZB_LE_H

/* A 16-bit value that is stored in two octets, least significant octet first, at p. */
#define ZB_LE_U16(p) ((uint16_t)(p)[0] | ((uint16_t)(p)[1] << 8))

#endif /* _ZB_LE_H */
