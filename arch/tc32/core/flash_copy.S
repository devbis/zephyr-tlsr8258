/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(tc32_flash_read)

/*
 * void tc32_flash_read(uintptr_t addr, uint8_t *dst, size_t len)
 *
 * Runs from the TLSR boot-copied 0x880000 alias. Keep this helper independent
 * from C runtime state and literal pools: early .rodata/.data copy happens
 * before RAM sections are valid.
 *
 * The caller passes flash-visible LMAs or other readable source addresses and
 * this helper copies them into SRAM while still executing from the boot-copied
 * 0x880000 alias. Keep it independent from C runtime state and literal pools.
 */
SECTION_FUNC(tc32_flash_copy, tc32_flash_read)
.Lword_loop:
	tcmp r2, #4
	nop
	nop
	tjlo .Lread_loop
	tloadr r3, [r0]
	nop
	nop
	tstorer r3, [r1]
	tmov r3, #4
	tsub r2, r2, r3
	tadd r0, r0, r3
	tadd r1, r1, r3
	tj .Lword_loop

.Lread_loop:
	tcmp r2, #0
	nop
	nop
	tjeq .Ldone
	tloadrb r3, [r0]
	nop
	nop
	tstorerb r3, [r1]
	tmov r3, #1
	tsub r2, r2, r3
	tadd r0, r0, r3
	tadd r1, r1, r3
	tj .Lread_loop

.Ldone:
	tjex lr
