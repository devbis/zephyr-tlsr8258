/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_ARCH_TC32_INCLUDE_TC32_CONTEXT_H_
#define ZEPHYR_ARCH_TC32_INCLUDE_TC32_CONTEXT_H_

#ifdef _ASMLANGUAGE

#include <offsets_short.h>

/*
 * Install the callee-saved context of the thread in r0 (sp, lr, r4-r12)
 * and jump to its lr. r2 is clobbered, r0, r1 and r3 are passed through.
 */
.macro TC32_THREAD_RESUME
	tloadr r2, [r0, #_thread_offset_to_sp]
	nop
	nop
	tmov r13, r2
	tloadr r2, [r0, #_thread_offset_to_lr]
	nop
	nop
	tmov r14, r2
	tloadr r4, [r0, #_thread_offset_to_r4]
	tloadr r5, [r0, #_thread_offset_to_r5]
	tloadr r6, [r0, #_thread_offset_to_r6]
	tloadr r7, [r0, #_thread_offset_to_r7]
	tloadr r2, [r0, #_thread_offset_to_r8]
	nop
	nop
	tmov r8, r2
	tloadr r2, [r0, #_thread_offset_to_r9]
	nop
	nop
	tmov r9, r2
	tloadr r2, [r0, #_thread_offset_to_r10]
	nop
	nop
	tmov r10, r2
	tloadr r2, [r0, #_thread_offset_to_r11]
	nop
	nop
	tmov r11, r2
	tloadr r2, [r0, #_thread_offset_to_r12]
	nop
	nop
	tmov r12, r2
	tjex lr
.endm

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_ARCH_TC32_INCLUDE_TC32_CONTEXT_H_ */
