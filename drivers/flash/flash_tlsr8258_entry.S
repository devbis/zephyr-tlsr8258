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
 */

SECTION_FUNC(ram_code, tlsr8258_flash_call_write_pages)
	tloadr r4, .Lwrite_pages_target
	nop
	nop
	tjex r4

.Lwrite_pages_target:
	.word tlsr8258_flash_write_pages

SECTION_FUNC(ram_code, tlsr8258_flash_call_erase_sector_locked)
	tloadr r4, .Lerase_sector_target
	nop
	nop
	tjex r4

.Lerase_sector_target:
	.word tlsr8258_flash_erase_sector_locked
