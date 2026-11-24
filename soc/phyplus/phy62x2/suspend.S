/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The registers that a function must keep, saved and restored like setjmp() and longjmp() of
 * the C library, for the suspend to RAM of the PHY62x2. The core is Cortex-M0 (Thumb-1): r8 to r11
 * go through low registers.
 *
 * The layout of the context, offsets in bytes:
 *
 *    0 r4    4 r5    8 r6   12 r7   16 r8   20 r9   24 r10   28 r11
 *   32 MSP  36 PSP  40 CONTROL  44 PRIMASK  48 LR
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

_ASM_FILE_PROLOGUE

GTEXT(phy62x2_ctx_save)
GTEXT(phy62x2_ctx_restore)

/* int phy62x2_ctx_save(uint32_t *ctx): returns 0, and 1 when the context is restored */
SECTION_FUNC(TEXT, phy62x2_ctx_save)
	stmia	r0!, {r4, r5, r6, r7}
	mov	r1, r8
	mov	r2, r9
	mov	r3, r10
	mov	r4, r11
	stmia	r0!, {r1, r2, r3, r4}
	mrs	r1, MSP
	mrs	r2, PSP
	mrs	r3, CONTROL
	mrs	r4, PRIMASK
	stmia	r0!, {r1, r2, r3, r4}
	mov	r1, lr
	str	r1, [r0, #0]
	/* r4 was the last that was needed; put back what the caller expects of it */
	subs	r0, #48
	ldmia	r0!, {r4}
	movs	r0, #0
	bx	lr

/* void phy62x2_ctx_restore(const uint32_t *ctx): does not return, continues after phy62x2_ctx_save() */
SECTION_FUNC(TEXT, phy62x2_ctx_restore)
	adds	r0, #16
	ldmia	r0!, {r1, r2, r3, r4}
	mov	r8, r1
	mov	r9, r2
	mov	r10, r3
	mov	r11, r4
	ldmia	r0!, {r1, r2, r3, r4}
	msr	MSP, r1
	msr	PSP, r2
	msr	CONTROL, r3
	isb
	msr	PRIMASK, r4
	ldr	r1, [r0, #0]
	mov	lr, r1
	subs	r0, #48
	ldmia	r0!, {r4, r5, r6, r7}
	movs	r0, #1
	bx	lr
