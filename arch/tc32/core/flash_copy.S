/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(tc32_flash_read)

.macro TC32_MSPI_WAIT
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
.endm

/*
 * void tc32_flash_read(uintptr_t addr, uint8_t *dst, size_t len)
 *
 * Runs from the TLSR boot-copied 0x880000 alias. Do not use literal pools
 * here: early flash reads are needed before .data/.rodata are valid, and
 * literal loads from the 0x880000 alias are not reliable on TC32.
 *
 * The Zephyr boot image data LMA is below 64 KiB at this stage, so derive the
 * 24-bit flash address as 00:addr[15:8]:addr[7:0]. Avoid right shifts in this
 * early helper; the current TC32 path has shown bad hardware results for that
 * sequence while debugging startup copy.
 */
SECTION_FUNC(tc32_flash_copy, tc32_flash_read)
	tpush {r4, r5, r6, r7}

	/* r3 = reg_mspi_data (0x0080000c), r4 = reg_mspi_ctrl (0x0080000d) */
	tmov r3, #1
	tshftl r3, r3, #23
	tmov r4, #12
	tadd r3, r3, r4
	tmov r4, #1
	tshftl r4, r4, #23
	tmov r5, #13
	tadd r4, r4, r5

	tmov r5, #1
	tstorerb r5, [r4]
	nop
	nop
	nop
	nop
	tmov r5, #0
	tstorerb r5, [r4]

	tmov r5, #3
	tstorerb r5, [r3]
	TC32_MSPI_WAIT

	tmov r5, #0
	tstorerb r5, [r3]
	TC32_MSPI_WAIT

	tadd r7, r0, #0
	tmov r6, #1
	tshftl r6, r6, #8
	tmov r5, #0
.Laddr_mid_loop:
	tcmp r7, r6
	nop
	nop
	tjlo .Laddr_mid_done
	tsub r7, r7, r6
	tadd r5, #1
	tj .Laddr_mid_loop
.Laddr_mid_done:
	tstorerb r5, [r3]
	TC32_MSPI_WAIT

	tstorerb r7, [r3]
	TC32_MSPI_WAIT

	tmov r5, #0
	tstorerb r5, [r3]
	TC32_MSPI_WAIT

	tmov r5, #10
	tstorerb r5, [r4]
	TC32_MSPI_WAIT

.Lread_loop:
	tcmp r2, #0
	nop
	nop
	tjeq .Ldone
	tloadrb r5, [r3]
	nop
	nop
	tstorerb r5, [r1]
	TC32_MSPI_WAIT
	tsub r2, #1
	tadd r1, #1
	tj .Lread_loop

.Ldone:
	tmov r5, #1
	tstorerb r5, [r4]
	tpop {r4, r5, r6, r7}
	nop
	nop
	tjex lr
