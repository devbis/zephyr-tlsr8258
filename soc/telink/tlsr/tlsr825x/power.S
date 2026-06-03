/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(start_suspend)

SECTION_FUNC(boot_ram_mirror, start_suspend)
	tpush {r2-r3}
	tmovs r2, #129
	tloadr r3, .Lsuspend_data
	tstorerb r2, [r3]
	.rept 64
	tmov r8, r8
	.endr
	tpop {r2-r3}
	tjex lr

	.align 2
.Lsuspend_data:
	.word 0x0080006f
