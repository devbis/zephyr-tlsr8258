/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Z-Stack derives the unique link key of a device from one seed, rotated by a
 * per-device shift and mixed with the device address. A host stores the shift
 * and the address, and expects the key back.
 */
#include <string.h>

#include "zb_znp_internal.h"

void znp_tclk_key(const uint8_t seed[16], uint8_t shift, const uint8_t ieee[8], uint8_t key[16])
{
	for (size_t i = 0; i < 16U; i++) {
		key[i] = seed[(i + shift) % 16U] ^ ieee[i % 8U];
	}
}
