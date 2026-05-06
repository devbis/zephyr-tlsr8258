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
GTEXT(arch_data_copy)
GTEXT(z_cstart)
GTEXT(z_tc32_handle_irqs)
GTEXT(z_get_next_switch_handle)
GTEXT(z_tc32_switch)

SECTION_FUNC(vectors, __start)
	.org 0x0
	tj __reset
	.word 0

	.org 0x8
	.word 0x544c4e4b
	.word 0x00880000 + _ramcode_size_div_16_align_256_

	.org 0x10
	tj __irq

	.org 0x20
__reset:
	tloadr r0, .Lirq_en
	tmov r1, #0
	tstorerb r1, [r0]
	tloadr r0, .Lirq_mask
	tstorer r1, [r0]
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
.Lirq_mask:
	.word 0x00800640

	.align 2
z_prep_c:
	/*
	 * Keep early startup in the low forward-branch area. TC32 free-run
	 * debugging has shown unstable behavior around early backward tjl/call
	 * returns before C runtime setup.
	 *
	 * Match soc_prep_hook(): clear watchdog enable in reg_tmr_ctrl
	 * (0x800620 bit 23) before BSS/data setup.
	 */
	tmov r0, #1
	tshftl r0, r0, #23
	tmov r1, #1
	tshftl r1, r1, #23
	tmov r2, #6
	tshftl r2, r2, #8
	tadd r1, r1, r2
	tmov r2, #32
	tadd r1, r1, r2
	tloadr r2, [r1]
	nop
	nop
	tbclr r2, r0
	tstorer r2, [r1]

	tloadr r0, .Lbss_start
	tloadr r2, .Lbss_end
	nop
	nop
	tsub r2, r2, r0
	tmov r1, #0
.Lbss_loop:
	tcmp r2, #0
	nop
	nop
	tjeq .Lbss_done
	tstorerb r1, [r0]
	tadd r0, #1
	tsub r2, #1
	tj .Lbss_loop
.Lbss_done:

	tjl arch_data_copy
	nop
	nop
	nop
	nop

	tjl z_cstart

.Lprep_hang:
	tj .Lprep_hang

	.align 2
.Lbss_start:
	.word __bss_start
.Lbss_end:
	.word __bss_end

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
