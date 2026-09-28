/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>
#include <tc32_context.h>
#include <tlsr825x/irq.h>

GTEXT(z_tc32_switch)
GTEXT(z_tc32_thread_start)
GTEXT(z_tc32_switch_to_main)
GTEXT(z_thread_entry)
GDATA(z_tc32_irq_soft_masked)

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

	tmov r2, #___thread_t_switch_handle_OFFSET
	tadd r2, r2, r1
	tstorer r1, [r2]

	TC32_THREAD_RESUME

/*
 * First switch into a thread built by arch_new_thread(): unpack the
 * synthetic frame into the z_thread_entry() arguments.
 */
SECTION_FUNC(TEXT, z_tc32_thread_start)
	tmov r5, r13
	tloadr r0, [r5, #__struct_arch_esf_r0_OFFSET]
	tloadr r1, [r5, #__struct_arch_esf_r1_OFFSET]
	tloadr r2, [r5, #__struct_arch_esf_r2_OFFSET]
	tloadr r3, [r5, #__struct_arch_esf_r3_OFFSET]
	tloadr r4, [r5, #__struct_arch_esf_sr_OFFSET]
	tadd r5, r5, #__struct_arch_esf_SIZEOF
	tmov r13, r5
	/*
	 * A thread is switched to with reg_irq_en cleared, and it enters here
	 * instead of returning into do_swap(), so nothing else will restore
	 * the gate for it. Publish the state arch_new_thread() recorded in the
	 * synthetic frame, the way z_tc32_switch_to_main does for main.
	 */
	tloadr r5, .Lthread_start_soft_masked
	nop
	nop
	tmov r6, #0
	tstorerb r6, [r5]
	tloadr r5, .Lthread_start_irq_en
	nop
	nop
	tstorerb r4, [r5]
	/* tjl for its range; z_thread_entry() does not return. */
	tjl z_thread_entry

	.align 2
.Lthread_start_soft_masked:
	.word z_tc32_irq_soft_masked
.Lthread_start_irq_en:
	.word TLSR8258_REG_IRQ_EN_ADDR

/* void z_tc32_switch_to_main(char *stack_ptr, k_thread_entry_t entry) */
SECTION_FUNC(TEXT, z_tc32_switch_to_main)
	tmov r13, r0
	tmov r0, r1
	tmov r1, #0
	tmov r2, #0
	tmov r3, #0
	tloadr r4, .Lswitch_main_soft_masked
	nop
	nop
	tstorerb r1, [r4]
	tloadr r4, .Lswitch_main_irq_en
	nop
	nop
	tmov r5, #1
	tstorerb r5, [r4]
	/* tjl for its range; z_thread_entry() does not return. */
	tjl z_thread_entry

	.align 2
.Lswitch_main_soft_masked:
	.word z_tc32_irq_soft_masked
.Lswitch_main_irq_en:
	.word TLSR8258_REG_IRQ_EN_ADDR
