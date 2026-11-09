/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Minimal sys_rand_get() backend for the radios that have no Zephyr entropy
 * driver, the TLSR8269 at present.
 *
 * Without an entropy driver no CONFIG_*_RANDOM_GENERATOR is available. The ZCL
 * layer calls sys_rand_get() (via zb_random()) for ZCL sequence numbers and
 * ZLL commissioning, and the radio driver for an extended address that is not
 * in the flash yet; none of them needs cryptographic strength, but all units
 * that start alike get the same numbers.
 *
 * This file provides z_impl_sys_rand_get() using a 64-bit LCG seeded
 * and mixed with k_cycle_get_32() on every call.  It is deliberately
 * NOT a Kconfig-selected implementation so that it compiles unconditionally
 * and does not add significant BSS/data overhead.
 *
 * NOTE: Not suitable for security-sensitive operations.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

void z_impl_sys_rand_get(void *dst, size_t len)
{
	static uint64_t state = 123456789ULL;
	uint8_t *out = (uint8_t *)dst;

	while (len > 0) {
		state += k_cycle_get_32();
		state = state * 6364136223846793005ULL + 1442695040888963407ULL;
		uint32_t val = (uint32_t)(state >> 33);
		size_t chunk = (len < sizeof(val)) ? len : sizeof(val);

		memcpy(out, &val, chunk);
		out += chunk;
		len -= chunk;
	}
}
