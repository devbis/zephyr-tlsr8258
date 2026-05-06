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
 * The image header asks the TLSR boot ROM to copy the complete ROM data LMA
 * range into SRAM. TC32 executes that boot copy through the 0x880000 alias, but
 * data loads must use the 0x840000 SRAM address view. Low flash/code data loads
 * produced stale opcode bytes during early startup debug. Avoid manual MSPI here
 * as well; vendor flash access requires a full CS/wait sequence and using it
 * this early stalled the CPU while still inside the RAM helper.
 */
SECTION_FUNC(tc32_flash_copy, tc32_flash_read)
	tmov r3, #0x84
	tshftl r3, r3, #16
	tadd r0, r0, r3

.Lread_loop:
	tcmp r2, #0
	nop
	nop
	tjeq .Ldone
	tloadrb r3, [r0]
	nop
	nop
	tstorerb r3, [r1]
	tsub r2, #1
	tadd r0, #1
	tadd r1, #1
	tj .Lread_loop

.Ldone:
	tjex lr
