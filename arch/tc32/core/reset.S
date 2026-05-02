/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(__start)
GTEXT(__irq)
GTEXT(z_tc32_irq_entry)
GTEXT(z_prep_c)
GTEXT(z_tc32_handle_irqs)

SECTION_FUNC(vectors, __start)
	tloadr r0, .Lstack_end
	tmov sp, r0
	tjl z_prep_c
.Lhang:
	tj .Lhang

	.align 2
.Lstack_end:
	.word _stack_end_

SECTION_FUNC(.ram_code, __irq)
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

	tpop {r0-r5}
	tmov r8, r1
	tmov r9, r2
	tmov r10, r3
	tmov r11, r4
	tmov r12, r5

	tmssr r0
	tpop {r0-r7}
	treti {r15}
