/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <offsets_short.h>
#include <tc32_context.h>
#include <tlsr825x/irq.h>
#include <tlsr825x/soc.h>

/*
 * CPSR values for IRQ and SVC mode with the I bit set, used to step between
 * the banked stacks with interrupts masked, and with it clear. Then the
 * geometry of the frame z_tc32_irq_entry pushes: r0-r7, r8-r12, the saved
 * status register and the return address.
 */
#define TC32_MODE_IRQ          0x92
#define TC32_MODE_SVC          0x93
#define TC32_MODE_IRQ_IRQ_ON   0x12
#define TC32_MODE_SVC_IRQ_ON   0x13
#define TC32_BOOT_SIGNATURE    0x544c4e4b
#define TC32_BOOT_COPY_FLAGS   0x00880000

#define TC32_IRQ_FRAME_WORDS   15
#define TC32_IRQ_FRAME_SIZE    (TC32_IRQ_FRAME_WORDS * 4)

GTEXT(__start)
GTEXT(__reset)
GTEXT(__irq)
GTEXT(z_tc32_irq_entry)
GTEXT(z_prep_c)
GTEXT(z_tc32_handle_irqs)
GDATA(z_interrupt_stacks)
GDATA(z_tc32_switch_from)
GDATA(z_tc32_irq_soft_masked)

SECTION_FUNC(vectors, __start)
	.org 0x0
	tj __reset
	.word 0

	/*
	 * Boot header read by the ROM: the "KNLT" signature, then the size of
	 * the image head it copies to SRAM, in 16-byte units, in the low half
	 * word, with the vendor's fixed flags 0x0088 above it.
	 */
	.org 0x8
	.word TC32_BOOT_SIGNATURE
	.word TC32_BOOT_COPY_FLAGS + _tc32_boot_copy_size_div_16_align_256_

	.org 0x10
	tj __irq

	.org 0x18
	/* Reserved boot-header words; the ROM does not read them. */
	.word 0
	.word 0

	.org 0x20
/*
 * The boot ROM enters here from its SRAM copy of the image head, with the
 * flash still in deep power-down and the instruction cache unconfigured.
 * Only this prefix may run before both are set up; z_prep_c() and
 * everything after it execute from flash.
 */
__reset:
	tloadr r0, .Lirq_mode
	tmcsr r0
	tloadr r0, .Lstack_end
	tmov sp, r0

	/*
	 * The SVC stack is the interrupt stack until
	 * arch_switch_to_main_thread() moves to the main thread stack.
	 */
	tloadr r0, .Lsvc_mode
	tmcsr r0
	tloadr r0, .Lstack_end
	tmov sp, r0

	tloadr r0, .Lirq_en
	tmov r1, #0
	tstorerb r1, [r0]
	/*
	 * reg_irq_mask is 24 bits and reg_irq_en is the byte above it, so this
	 * word store spans both. It is safe only because it stores a constant
	 * zero, which is what the byte store above already put in the gate;
	 * anything that reads the mask back and writes it again has to use the
	 * byte accessors in <tlsr825x/irq.h> instead.
	 */
	tloadr r0, .Lirq_mask
	tstorer r1, [r0]

	/* Clear the instruction cache tags and lock the image head lines. */
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

	/*
	 * Release the flash from deep power-down (command 0xab) and give it
	 * the few cycles tRES1 needs before the first fetch.
	 */
	tloadr r1, .Lmspi_data
	tmov r0, #0
	tstorerb r0, [r1, #TLSR8258_MSPI_CTRL_OFFSET]
	tmov r0, #TLSR8258_FLASH_CMD_RELEASE_PD
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
	tmov r0, #TLSR8258_MSPI_CTRL_CS
	tstorerb r0, [r1, #TLSR8258_MSPI_CTRL_OFFSET]

	/* Leave the SRAM copy: z_prep_c() is linked at its flash address. */
	tloadr r0, .Lprep_c
	nop
	nop
	tjex r0

	.align 2
.Lirq_mode:
	.word TC32_MODE_IRQ_IRQ_ON
.Lsvc_mode:
	.word TC32_MODE_SVC_IRQ_ON
.Lstack_end:
	.word z_interrupt_stacks + __z_interrupt_stack_SIZEOF
.Lirq_en:
	.word TLSR8258_REG_IRQ_EN_ADDR
.Lirq_mask:
	.word TLSR8258_REG_IRQ_MASK_ADDR
.Lictag_start:
	.word _ictag_start_
.Lictag_end:
	.word _ictag_end_
.Licache_lines:
	.word _tc32_icache_lines_
.Licache_ctrl:
	.word TLSR8258_REG_ICACHE_CTRL_ADDR
.Lmspi_data:
	.word TLSR8258_REG_MSPI_DATA_ADDR
.Lprep_c:
	.word z_prep_c

SECTION_FUNC(boot_ram_mirror, __irq)
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
	/*
	 * r0 is the thread to run, or NULL to resume the interrupted one.
	 * Every low register is dead here: the interrupted values are in the
	 * frame below and the C dispatcher has clobbered the rest.
	 */
	tcmp r0, #0
	nop
	nop
	tjeq .Lno_irq_reschedule

	/*
	 * The frame sits on the banked IRQ stack, which the next interrupt
	 * reuses, so it has to move onto the outgoing thread's own stack
	 * before anyone else runs. Reaching that stack means borrowing SVC
	 * mode for the two instructions it takes to read and lower its sp.
	 */
	tmov r4, r13

	tmov r5, #TC32_MODE_SVC
	tmcsr r5
	tmov r5, r13
	tmov r6, #TC32_IRQ_FRAME_SIZE
	tsub r5, r5, r6
	tmov r13, r5
	tmov r6, #TC32_MODE_IRQ
	tmcsr r6

	tmov r3, r5
	tmov r6, #TC32_IRQ_FRAME_WORDS
.Lirq_frame_out:
	tloadr r7, [r4]
	tstorer r7, [r5]
	tmov r2, #4
	tadd r4, r4, r2
	tadd r5, r5, r2
	tmov r2, #1
	tsub r6, r6, r2
	tcmp r6, #0
	nop
	nop
	tjne .Lirq_frame_out

	tmov r4, r13
	tmov r5, #TC32_IRQ_FRAME_SIZE
	tadd r4, r4, r5
	tmov r13, r4

	/* Park the outgoing thread on .Lirq_unwind with the relocated frame. */
	tloadr r1, .Lswitch_from
	nop
	nop
	tloadr r1, [r1]
	tstorer r3, [r1, #_thread_offset_to_sp]
	tloadr r2, .Lirq_unwind_ptr
	nop
	nop
	tstorer r2, [r1, #_thread_offset_to_lr]

	/*
	 * Hand over the way every other switch in this port does: with
	 * reg_irq_en shut, so whatever the incoming thread resumes into
	 * reopens the gate itself (arch_irq_unlock() in do_swap(),
	 * z_tc32_thread_start(), or .Lirq_unwind below). Shutting it here also
	 * covers the register load, which runs with the I bit already dropped
	 * because a thread must not be entered with it set.
	 */
	tloadr r1, .Lresched_irq_en
	nop
	nop
	tmov r2, #0
	tstorerb r2, [r1]
	nop
	nop

	/* Become the incoming thread; its stack is an SVC-mode stack. */
	tmov r1, #TC32_MODE_SVC_IRQ_ON
	tmcsr r1

	TC32_THREAD_RESUME

/*
 * A thread preempted inside an interrupt resumes here, in SVC mode, with the
 * frame on its own stack. Put it back where treti expects to find it.
 */
.Lirq_unwind:
	tmov r4, r13
	tmov r5, #TC32_IRQ_FRAME_SIZE
	tadd r5, r4, r5
	tmov r13, r5

	tmov r5, #TC32_MODE_IRQ
	tmcsr r5
	tmov r5, r13
	tmov r6, #TC32_IRQ_FRAME_SIZE
	tsub r5, r5, r6
	tmov r13, r5

	tmov r6, #TC32_IRQ_FRAME_WORDS
.Lirq_frame_in:
	tloadr r7, [r4]
	tstorer r7, [r5]
	tmov r2, #4
	tadd r4, r4, r2
	tadd r5, r5, r2
	tmov r2, #1
	tsub r6, r6, r2
	tcmp r6, #0
	nop
	nop
	tjne .Lirq_frame_in

	/*
	 * Same reason z_tc32_thread_start publishes the gate: this path does
	 * not return into do_swap(), so nothing else will reopen reg_irq_en
	 * for the thread. z_tc32_handle_irqs() only switches away from a
	 * thread outside an irq_lock() section, so the gate was open and the
	 * soft mask clear when it was interrupted. The store is safe
	 * here and nowhere earlier: IRQ mode still holds the I bit, so the
	 * frame below sp cannot be reused until treti drops it.
	 */
	tloadr r4, .Lsoft_masked_ptr
	nop
	nop
	tmov r5, #0
	tstorerb r5, [r4]
	tloadr r4, .Lresched_irq_en
	nop
	nop
	tmov r5, #1
	tstorerb r5, [r4]

.Lno_irq_reschedule:
#endif

.Lirq_restore:
	/*
	 * Handlers reopen reg_irq_en before returning. Keep it closed if the
	 * interrupt arrived inside an irq_lock() section, see
	 * z_tc32_irq_soft_masked.
	 */
	tloadr r1, .Lsoft_masked_ptr
	nop
	nop
	tloadrb r0, [r1]
	nop
	nop
	tcmp r0, #0
	tjeq 1f
	tloadr r1, .Lresched_irq_en
	nop
	nop
	tmov r0, #0
	tstorerb r0, [r1]
1:
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
.Lswitch_from:
	.word z_tc32_switch_from
.Lirq_unwind_ptr:
	.word .Lirq_unwind
.Lresched_irq_en:
	.word TLSR8258_REG_IRQ_EN_ADDR
.Lsoft_masked_ptr:
	.word z_tc32_irq_soft_masked
#endif
