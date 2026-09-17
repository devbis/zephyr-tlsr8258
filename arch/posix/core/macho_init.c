/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __APPLE__

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <kernel_internal.h>

extern const struct mach_header_64 _mh_execute_header;

/* Keep an empty GNU constructor range materialized in the embedded image. */
static void z_macho_init_array_sentinel_fn(void)
{
}

static void (*const z_macho_init_array_sentinel)(void)
	__attribute__((used, section("__DATA,__mod_init_func"))) =
		z_macho_init_array_sentinel_fn;

/*
 * Mach-O keeps each init priority in a separate section. Collecting and
 * sorting the section ranges recreates the single ordered sequence provided
 * by the ELF linker script.
 */
struct macho_init_section {
	const struct init_entry *start;
	size_t count;
	unsigned int prio;
	unsigned int sub_prio;
};

static bool macho_parse_init_section_name(const char *sectname, unsigned int level_ord,
					  unsigned int *prio, unsigned int *sub_prio)
{
	char prefix[] = "zi0_";
	const char *cursor;
	unsigned int parsed_prio = 0U;
	unsigned int parsed_sub_prio = 0U;

	prefix[2] = (char)('0' + level_ord);

	if (strncmp(sectname, prefix, strlen(prefix)) != 0) {
		return false;
	}

	cursor = sectname + strlen(prefix);
	if ((*cursor < '0') || (*cursor > '9')) {
		return false;
	}

	while ((*cursor >= '0') && (*cursor <= '9')) {
		parsed_prio = (parsed_prio * 10U) + (unsigned int)(*cursor - '0');
		cursor++;
	}

	if (*cursor == '_') {
		cursor++;
		while ((*cursor >= '0') && (*cursor <= '9')) {
			parsed_sub_prio = (parsed_sub_prio * 10U) +
					  (unsigned int)(*cursor - '0');
			cursor++;
		}
	}

	*prio = parsed_prio;
	*sub_prio = parsed_sub_prio;

	return true;
}

static size_t macho_collect_init_sections(unsigned int level_ord,
					  struct macho_init_section *sections,
					  size_t max_sections)
{
	const struct mach_header_64 *hdr = &_mh_execute_header;
	const struct load_command *lc =
		(const struct load_command *)((const char *)hdr + sizeof(*hdr));
	const intptr_t slide = _dyld_get_image_vmaddr_slide(0);
	size_t count = 0U;

	for (uint32_t i = 0U; i < hdr->ncmds; i++) {
		if (lc->cmd == LC_SEGMENT_64) {
			const struct segment_command_64 *seg =
				(const struct segment_command_64 *)lc;
			const struct section_64 *sec =
				(const struct section_64 *)(seg + 1);

			if (strncmp(seg->segname, "__DATA", sizeof(seg->segname)) == 0) {
				for (uint32_t j = 0U; (j < seg->nsects) && (count < max_sections);
				     j++, sec++) {
					char sectname[sizeof(sec->sectname) + 1];
					unsigned int prio;
					unsigned int sub_prio;

					memcpy(sectname, sec->sectname, sizeof(sec->sectname));
					sectname[sizeof(sec->sectname)] = '\0';

					if (!macho_parse_init_section_name(sectname, level_ord,
									   &prio, &sub_prio)) {
						continue;
					}

					sections[count].start =
						(const struct init_entry *)(uintptr_t)(sec->addr + slide);
					sections[count].count = sec->size / sizeof(struct init_entry);
					sections[count].prio = prio;
					sections[count].sub_prio = sub_prio;
					count++;
				}
			}
		}

		lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
	}

	return count;
}

static void macho_sort_init_sections(struct macho_init_section *sections, size_t count)
{
	for (size_t i = 1U; i < count; i++) {
		struct macho_init_section key = sections[i];
		size_t j = i;

		while ((j > 0U) &&
		       ((sections[j - 1U].prio > key.prio) ||
			((sections[j - 1U].prio == key.prio) &&
			 (sections[j - 1U].sub_prio > key.sub_prio)))) {
			sections[j] = sections[j - 1U];
			j--;
		}

		sections[j] = key;
	}
}

void arch_sys_init_run_level(unsigned int level)
{
	struct macho_init_section sections[64];
	size_t count = macho_collect_init_sections(level, sections, ARRAY_SIZE(sections));

	macho_sort_init_sections(sections, count);

	for (size_t i = 0U; i < count; i++) {
		for (size_t j = 0U; j < sections[i].count; j++) {
			z_sys_init_run_entry(&sections[i].start[j], level);
		}
	}
}

#endif /* __APPLE__ */
