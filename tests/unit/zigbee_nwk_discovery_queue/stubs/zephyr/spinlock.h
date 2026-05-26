/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

typedef int k_spinlock_key_t;
struct k_spinlock { int unused; };

static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l)
{
	(void)l;
	return 0;
}

static inline void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t key)
{
	(void)l;
	(void)key;
}
