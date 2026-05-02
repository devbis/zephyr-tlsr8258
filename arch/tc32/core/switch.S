/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>

GTEXT(z_tc32_switch)
GTEXT(z_tc32_thread_start)

/* void z_tc32_switch(struct k_thread *new, struct k_thread *old) */
SECTION_FUNC(TEXT, z_tc32_switch)
	tmov r2, r13
	tstorer r2, [r1, #_thread_offset_to_sp]
	tmov r2, r14
	tstorer r2, [r1, #_thread_offset_to_lr]
	tstorer r4, [r1, #_thread_offset_to_r4]
	tstorer r5, [r1, #_thread_offset_to_r5]
	tstorer r6, [r1, #_thread_offset_to_r6]
	tstorer r7, [r1, #_thread_offset_to_r7]
	tmov r2, r8
	tstorer r2, [r1, #_thread_offset_to_r8]
	tmov r2, r9
	tstorer r2, [r1, #_thread_offset_to_r9]
	tmov r2, r10
	tstorer r2, [r1, #_thread_offset_to_r10]
	tmov r2, r11
	tstorer r2, [r1, #_thread_offset_to_r11]
	tmov r2, r12
	tstorer r2, [r1, #_thread_offset_to_r12]

	tstorer r1, [r1, #___thread_t_switch_handle_OFFSET]

	tloadr r2, [r0, #_thread_offset_to_sp]
	tmov r13, r2
	tloadr r2, [r0, #_thread_offset_to_lr]
	tmov r14, r2
	tloadr r4, [r0, #_thread_offset_to_r4]
	tloadr r5, [r0, #_thread_offset_to_r5]
	tloadr r6, [r0, #_thread_offset_to_r6]
	tloadr r7, [r0, #_thread_offset_to_r7]
	tloadr r2, [r0, #_thread_offset_to_r8]
	tmov r8, r2
	tloadr r2, [r0, #_thread_offset_to_r9]
	tmov r9, r2
	tloadr r2, [r0, #_thread_offset_to_r10]
	tmov r10, r2
	tloadr r2, [r0, #_thread_offset_to_r11]
	tmov r11, r2
	tloadr r2, [r0, #_thread_offset_to_r12]
	tmov r12, r2
	tjex lr

SECTION_FUNC(TEXT, z_tc32_thread_start)
	tmov r5, r13
	tloadr r0, [r5, #__struct_arch_esf_r0_OFFSET]
	tloadr r1, [r5, #__struct_arch_esf_r1_OFFSET]
	tloadr r2, [r5, #__struct_arch_esf_r2_OFFSET]
	tloadr r3, [r5, #__struct_arch_esf_r3_OFFSET]
	tloadr r4, [r5, #__struct_arch_esf_pc_OFFSET]
	tadd r5, r5, #__struct_arch_esf_SIZEOF
	tmov r13, r5
	tjex r4
