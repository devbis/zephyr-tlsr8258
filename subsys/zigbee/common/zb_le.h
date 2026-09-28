/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_LE_H
#define _ZB_LE_H

/* A 16-bit value that is stored in two octets, least significant octet first, at p. */
#define ZB_LE_U16(p) ((u16)(p)[0] | ((u16)(p)[1] << 8))

#endif /* _ZB_LE_H */
