/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_SECURITY_FLAGS_H
#define _SS_SECURITY_FLAGS_H

#include "zb_common.h"

/*
 * The vendor ABI stores these fields in the byte immediately before
 * activeKeySeqNum.  Accessing the SDK bit-fields directly does not preserve
 * that layout with the reconstruction compiler, so keep the ABI conversion
 * in one named helper.
 */
enum {
	SS_IB_SECURITY_LEVEL_MASK = 0x07U,
	SS_IB_SECURITY_LEVEL_ENCRYPTION_MASK = BIT(2),
	SS_IB_SECURE_ALL_FRESH_MASK = BIT(3),
	SS_IB_ACTIVE_MATERIAL_INDEX_MASK = 0x30U,
};

static inline u8 *ss_ib_security_flags_ptr(void)
{
	return &ss_ib.activeKeySeqNum - 1;
}

static inline u8 ss_ib_security_flags_get(void)
{
	return *ss_ib_security_flags_ptr();
}

static inline u8 ss_ib_security_level_get(void)
{
	return (u8)(ss_ib_security_flags_get() & SS_IB_SECURITY_LEVEL_MASK);
}

static inline void ss_ib_security_level_set(u8 level)
{
	u8 *flags = ss_ib_security_flags_ptr();

	*flags = (u8)((*flags & (u8)~SS_IB_SECURITY_LEVEL_MASK) |
		      (level & SS_IB_SECURITY_LEVEL_MASK));
}

static inline bool ss_ib_secure_all_fresh(void)
{
	return (ss_ib_security_flags_get() & SS_IB_SECURE_ALL_FRESH_MASK) != 0U;
}

static inline void ss_ib_secure_all_fresh_set(bool enabled)
{
	u8 *flags = ss_ib_security_flags_ptr();

	if (enabled) {
		*flags |= SS_IB_SECURE_ALL_FRESH_MASK;
	} else {
		*flags &= (u8)~SS_IB_SECURE_ALL_FRESH_MASK;
	}
}

static inline u8 ss_ib_active_secure_material_index_get(void)
{
	return (u8)((ss_ib_security_flags_get() & SS_IB_ACTIVE_MATERIAL_INDEX_MASK) >> 4);
}

static inline void ss_ib_active_secure_material_index_set(u8 index)
{
	u8 *flags = ss_ib_security_flags_ptr();

	*flags = (u8)((*flags & (u8)~SS_IB_ACTIVE_MATERIAL_INDEX_MASK) |
		      ((index << 4) & SS_IB_ACTIVE_MATERIAL_INDEX_MASK));
}

#endif
