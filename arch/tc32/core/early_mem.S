/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(arch_early_memset)
GTEXT(arch_early_memcpy)
GTEXT(tc32_delay_cycles)

/* void arch_early_memset(void *dst, int c, size_t n) */
SECTION_FUNC(TEXT, arch_early_memset)
.Lmemset_loop:
	tcmp r2, #0
	nop
	nop
	tjeq .Lmemset_done
	tstorerb r1, [r0]
	tmov r3, #1
	tadd r0, r0, r3
	tsub r2, r2, r3
	tj .Lmemset_loop
.Lmemset_done:
	tjex lr

/* void arch_early_memcpy(void *dst, const void *src, size_t n) */
SECTION_FUNC(TEXT, arch_early_memcpy)
.Lmemcpy_loop:
	tcmp r2, #0
	nop
	nop
	tjeq .Lmemcpy_done
	tloadrb r3, [r1]
	nop
	nop
	tstorerb r3, [r0]
	tmov r3, #1
	tadd r0, r0, r3
	tadd r1, r1, r3
	tsub r2, r2, r3
	tj .Lmemcpy_loop
.Lmemcpy_done:
	tjex lr

/* void tc32_delay_cycles(uint32_t cycles) */
SECTION_FUNC(TEXT, tc32_delay_cycles)
.Ldelay_loop:
	tcmp r0, #0
	nop
	nop
	tjeq .Ldelay_done
	tmov r1, #1
	tsub r0, r0, r1
	tj .Ldelay_loop
.Ldelay_done:
	tjex lr
