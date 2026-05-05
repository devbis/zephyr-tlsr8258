/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>

GTEXT(__start)
GTEXT(__reset)
GTEXT(__irq)
GTEXT(z_tc32_irq_entry)
GTEXT(z_prep_c)
GTEXT(z_tc32_handle_irqs)
GTEXT(z_get_next_switch_handle)
GTEXT(z_tc32_switch)

SECTION_FUNC(vectors, __start)
	.org 0x0
	tj __reset
	.word 0

	.org 0x8
	.word 0x544c4e4b
	.word 0x00880170

	.org 0x10
	tj __irq

	.org 0x20
__reset:
	tloadr r0, .Lirq_en
	tmov r1, #0
	tstorerb r1, [r0]
	tloadr r0, .Lstack_end
	tmov sp, r0
	tjl z_prep_c
.Lhang:
	tj .Lhang

	.align 2
.Lstack_end:
	.word _stack_end_
.Lirq_en:
	.word 0x00800643

SECTION_FUNC(ram_code, __irq)
z_tc32_irq_entry:
	tpush {r14}
	tpush {r0-r7}
	tmrss r0

	tmov r1, r8
	tmov r2, r9
	tmov r3, r10
	tmov r4, r11
	tmov r5, r12
	tpush {r0-r5}

	tjl z_tc32_handle_irqs

#ifdef CONFIG_MULTITHREADING
	tloadr r4, .Lkernel
	tloadr r4, [r4, #(___kernel_t_cpus_OFFSET + ___cpu_t_current_OFFSET)]
	tcmp r4, #0
	tjeq .Lno_irq_reschedule
	tloadr r5, .Lkernel
	tloadr r5, [r5, #(___kernel_t_ready_q_OFFSET + ___ready_q_t_cache_OFFSET)]
	tcmp r4, r5
	tjeq .Lno_irq_reschedule
	tmov r0, #0
	tjl z_get_next_switch_handle
	tcmp r0, #0
	tjeq .Lno_irq_reschedule
	tmov r1, r4
	tmov r2, r13
	tstorer r2, [r1, #_thread_offset_to_sp]
	tloadr r2, .Lirq_restore_ptr
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
.Lno_irq_reschedule:
#endif

.Lirq_restore:
	tpop {r0-r5}
	tmov r8, r1
	tmov r9, r2
	tmov r10, r3
	tmov r11, r4
	tmov r12, r5

	tmssr r0
	tpop {r0-r7}
	treti {r15}

#ifdef CONFIG_MULTITHREADING
	.align 2
.Lkernel:
	.word _kernel
.Lirq_restore_ptr:
	.word .Lirq_restore
#endif
