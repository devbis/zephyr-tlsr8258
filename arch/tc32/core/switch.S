/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>

GTEXT(z_tc32_switch)
GTEXT(z_tc32_thread_start)
GTEXT(z_tc32_switch_to_main_static)
GTEXT(z_tc32_switch_to_thread)
GTEXT(z_tc32_switch_to_main)
GTEXT(z_thread_entry)
GTEXT(main)

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
	tjl .Lthread_entry_call
.Lthread_returned:
	tj .Lthread_returned
.Lthread_entry_call:
	tjex r4

/* void z_tc32_switch_to_main_static(void) */
SECTION_FUNC(TEXT, z_tc32_switch_to_main_static)
	tloadr r0, .Lmain_thread
	tloadr r1, .Lkernel
	tstorer r0, [r1, #(___kernel_t_cpus_OFFSET + ___cpu_t_current_OFFSET)]
	tloadr r2, [r0, #_thread_offset_to_sp]
	tmov r13, r2
	tjl main
.Lmain_returned:
	tj .Lmain_returned

	.align 2
.Lmain_thread:
	.word z_main_thread
.Lkernel:
	.word _kernel

/* void z_tc32_switch_to_thread(struct k_thread *thread) */
SECTION_FUNC(TEXT, z_tc32_switch_to_thread)
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

/* void z_tc32_switch_to_main(char *stack_ptr, k_thread_entry_t entry) */
SECTION_FUNC(TEXT, z_tc32_switch_to_main)
	tmov r13, r0
	tmov r0, r1
	tmov r4, #1
	tbclr r0, r4
	tmov r1, #0
	tmov r2, #0
	tmov r3, #0
	tjl z_thread_entry
