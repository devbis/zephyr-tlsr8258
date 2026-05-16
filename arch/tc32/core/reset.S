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
GTEXT(z_cstart)
GTEXT(tc32_flash_read)
GTEXT(z_tc32_handle_irqs)
GTEXT(z_get_next_switch_handle)
GTEXT(z_tc32_switch)
GDATA(z_interrupt_stacks)

.macro TC32_ANALOG_WRITE addr, value
	tmov r0, #\addr
	tstorerb r0, [r1]
	nop
	nop
	tmov r0, #\value
	tstorerb r0, [r1, #1]
	nop
	nop
	tmov r0, #0x60
	tstorerb r0, [r1, #2]
	nop
	nop
1:
	tloadrb r0, [r1, #2]
	nop
	nop
	tmov r2, #1
	tand r0, r2
	tcmp r0, #0
	nop
	nop
	tjne 1b
	tmov r0, #0
	tstorerb r0, [r1, #2]
	nop
	nop
.endm

.macro TC32_COPY_FROM_SRAM_MIRROR
1:
	tcmp r2, #4
	nop
	nop
	tjlo 2f
	tloadr r3, [r0]
	nop
	nop
	tstorer r3, [r1]
	tmov r3, #4
	tsub r2, r2, r3
	tadd r0, r0, r3
	tadd r1, r1, r3
	tj 1b
2:
	tcmp r2, #0
	nop
	nop
	tjeq 3f
	tloadrb r3, [r0]
	nop
	nop
	tstorerb r3, [r1]
	tmov r3, #1
	tsub r2, r2, r3
	tadd r0, r0, r3
	tadd r1, r1, r3
	tj 2b
3:
.endm

SECTION_FUNC(vectors, __start)
	.org 0x0
	tj __reset
	.word 0

	.org 0x8
	.word 0x544c4e4b
	.word 0x00880000 + _tc32_boot_copy_size_div_16_align_256_

	.org 0x10
	tj __irq

	.org 0x18
.Lirq_low_call_target:
	.word z_tc32_handle_irqs
.Lirq_low_ret_target:
	.word .Lirq_restore

	.org 0x20
__reset:
	tloadr r0, .Lirq_mode
	tmcsr r0
	tloadr r0, .Lirq_stack_end
	tmov sp, r0

	tloadr r0, .Lsvc_mode
	tmcsr r0
	tloadr r0, .Lstack_end
	tmov sp, r0

	tloadr r0, .Lirq_en
	tmov r1, #0
	tstorerb r1, [r0]
	tloadr r0, .Lirq_mask
	tstorer r1, [r0]
	tjl z_prep_c
.Lhang:
	tj .Lhang

	.align 2
.Lirq_mode:
	.word 0x12
.Lsvc_mode:
	.word 0x13
.Lstack_end:
	/*
	 * Reuse the interrupt stack as the early SVC boot stack until
	 * arch_switch_to_main_thread() installs the main thread stack. Large
	 * TLSR8258 builds can leave only a few hundred bytes between _end and
	 * _stack_end_, which is not enough for C startup.
	 */
	.word z_interrupt_stacks + __z_interrupt_stack_SIZEOF
.Lirq_stack_end:
	.word z_interrupt_stacks + __z_interrupt_stack_SIZEOF
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
	 * Match the direct register part of vendor cpu_wakeup_init(): release
	 * peripheral resets and enable clock gates before PRE_KERNEL devices.
	 */
	tloadr r0, .Lictag_start
	tloadr r1, .Lictag_end
	nop
	nop
	tmov r2, #0
.Lictag_clear:
	tmov r3, r1
	tsub r3, r3, r0
	tcmp r3, #0
	nop
	nop
	tjeq .Lictag_done
	tstorer r2, [r0]
	tmov r3, #4
	tadd r0, r0, r3
	tj .Lictag_clear
.Lictag_done:
	tloadr r1, .Licache_ctrl
	tloadr r0, .Licache_lines
	nop
	nop
	tstorerb r0, [r1]
	tmov r2, #1
	tadd r0, r0, r2
	tstorerb r0, [r1, #1]

	tloadr r1, .Lrst_clk
	tloadr r0, .Lrst_clk_0
	nop
	nop
	tstorer r0, [r1]
	tloadr r0, .Lrst_clk_1
	nop
	nop
	tstorer r0, [r1, #4]
	tloadr r1, .Lflash_ctrl
	tmov r0, #0
	tstorerb r0, [r1, #1]
	tmov r0, #0xab
	tstorerb r0, [r1]
	tmov r3, #7
.Lflash_wakeup_delay:
	tcmp r3, #0
	nop
	nop
	tjeq .Lflash_wakeup_done
	tmov r2, #1
	tsub r3, r3, r2
	tj .Lflash_wakeup_delay
.Lflash_wakeup_done:
	tmov r0, #1
	tstorerb r0, [r1, #1]

	tloadr r1, .Lanalog_base
	TC32_ANALOG_WRITE 0x82, 0x64
	TC32_ANALOG_WRITE 0x34, 0x80
	TC32_ANALOG_WRITE 0x0b, 0x38
	TC32_ANALOG_WRITE 0x8c, 0x02
	TC32_ANALOG_WRITE 0x02, 0xa2

	/*
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

	/*
	 * The ROM entered us from the boot SRAM mirror. Switch to the canonical
	 * flash/XIP mapping before copying init sections from their LMAs.
	 */
	tloadr r0, .Lflash_prep_continue
	nop
	nop
	tjex r0

	.align 2
.Lflash_prep_continue:
	.word z_prep_c_flash_continue
.Lrst_clk:
	.word 0x00800060
.Lrst_clk_0:
	.word 0xff000000
.Lrst_clk_1:
	.word 0x0006ffff
.Lictag_start:
	.word _ictag_start_
.Lictag_end:
	.word _ictag_end_
.Licache_lines:
	.word _tc32_icache_lines_
.Licache_ctrl:
	.word 0x0080060c
.Lflash_ctrl:
	.word 0x0080000c
.Lanalog_base:
	.word 0x008000b8

SECTION_FUNC(TEXT, z_prep_c_flash_continue)
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
	tmov r3, #1
	tadd r0, r0, r3
	tsub r2, r2, r3
	tj .Lbss_loop
.Lbss_done:
#ifndef CONFIG_XIP
	tloadr r0, .Lrodata_lma_start
	tloadr r1, .Lrodata_start
	tloadr r2, .Lrodata_end
	nop
	nop
	tsub r2, r2, r1
	TC32_COPY_FROM_SRAM_MIRROR
#endif

	tloadr r0, .Ldata_lma_start
	tloadr r1, .Ldata_start
	tloadr r2, .Ldata_end
	nop
	nop
	tsub r2, r2, r1
	TC32_COPY_FROM_SRAM_MIRROR
	tjl z_cstart

.Lprep_hang:
	tj .Lprep_hang

	.align 2
.Lbss_start:
	.word __bss_start
.Lbss_end:
	.word __bss_end
.Lrodata_lma_start:
	.word __rodata_region_load_start
.Lrodata_start:
	.word __rodata_region_start
.Lrodata_end:
	.word __rodata_region_end
.Ldata_lma_start:
	.word __data_region_load_start
.Ldata_start:
	.word __data_region_start
.Ldata_end:
	.word __data_region_end

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

	/*
	 * IRQ vectors may execute from the TLSR boot mirror alias at 0x880000.
	 * A relative call from there would enter z_tc32_handle_irqs through the
	 * same alias, where PC-relative literal loads are not valid data reads.
	 * Registers are already saved, so branch by absolute low code address.
	 */
	tmov r1, #0x84
	tshftl r1, r1, #16
	tmov r0, #0x18
	tadd r1, r1, r0
	tloadr r0, [r1, #0x4]
	nop
	nop
	tmov r14, r0
	tloadr r0, [r1]
	nop
	nop
	tjex r0

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
