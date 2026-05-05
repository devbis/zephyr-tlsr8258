/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(arch_early_memset)
GTEXT(arch_early_memcpy)

/* void arch_early_memset(void *dst, int c, size_t n) */
SECTION_FUNC(TEXT, arch_early_memset)
	tcmp r2, #0
	tjeq .Lmemset_done
.Lmemset_loop:
	tstorerb r1, [r0]
	tadd r0, #1
	tsub r2, #1
	tcmp r2, #0
	tjne .Lmemset_loop
.Lmemset_done:
	tjex lr

/* void arch_early_memcpy(void *dst, const void *src, size_t n) */
SECTION_FUNC(TEXT, arch_early_memcpy)
	tcmp r2, #0
	tjeq .Lmemcpy_done
.Lmemcpy_loop:
	tloadrb r3, [r1]
	tstorerb r3, [r0]
	tadd r0, #1
	tadd r1, #1
	tsub r2, #1
	tcmp r2, #0
	tjne .Lmemcpy_loop
.Lmemcpy_done:
	tjex lr
