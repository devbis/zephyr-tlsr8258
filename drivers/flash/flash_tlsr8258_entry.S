/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>

GTEXT(tlsr8258_flash_call_write_pages)
GTEXT(tlsr8258_flash_call_erase_sector_locked)

/*
 * Small .ram_code veneers that jump straight into the high-RAM __ramfunc
 * helpers. This avoids the linker-generated __TC32ABSLongThunk_* stubs,
 * which currently wedge this chip on the first XIP -> __ramfunc transition
 * in the flash program/erase path.
 *
 * The target address is staged through r12 (the intra-procedure scratch
 * register), not r4. r4 is callee-saved: because these veneers tail-jump,
 * the callee restores the value the veneer had already destroyed, so the
 * ORIGINAL caller came back with r4 clobbered. tlsr8258_flash_erase() keeps
 * its `offset` in r4 across the call, so it resumed the erase loop with
 * offset = <veneer target>, and the next iteration erased
 * <veneer target> + 0x1000 -- a sector inside the code image.
 * r12 is not encodable by a Thumb-1 PC-relative load, so the address is
 * loaded into r0 and moved across, with r0 saved and restored.
 */

SECTION_FUNC(ram_code, tlsr8258_flash_call_write_pages)
	tpush {r0}
	tloadr r0, .Lwrite_pages_target
	nop
	nop
	tmov r12, r0
	tpop {r0}
	tjex r12

.align 2
.Lwrite_pages_target:
	.word tlsr8258_flash_write_pages

SECTION_FUNC(ram_code, tlsr8258_flash_call_erase_sector_locked)
	tpush {r0}
	tloadr r0, .Lerase_sector_target
	nop
	nop
	tmov r12, r0
	tpop {r0}
	tjex r12

.align 2
.Lerase_sector_target:
	.word tlsr8258_flash_erase_sector_locked
