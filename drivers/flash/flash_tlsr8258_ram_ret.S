/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(tlsr8258_flash_read_ram)
GTEXT(tlsr8258_flash_write_page_ram)
GTEXT(tlsr8258_flash_erase_sector_ram)

SECTION_FUNC(ram_code, tlsr8258_flash_read_ram)
	tpush {r14}
	tjl tlsr8258_flash_read_ram_body
	tpop {r1}
	tmov lr, r1
	tjex lr

SECTION_FUNC(ram_code, tlsr8258_flash_write_page_ram)
	tpush {r14}
	tjl tlsr8258_flash_write_page_ram_body
	tpop {r1}
	tmov lr, r1
	tjex lr

SECTION_FUNC(ram_code, tlsr8258_flash_erase_sector_ram)
	tpush {r14}
	tjl tlsr8258_flash_erase_sector_ram_body
	tpop {r1}
	tmov lr, r1
	tjex lr
