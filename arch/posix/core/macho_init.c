/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __APPLE__

#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <kernel_internal.h>

/* Keep GNU constructor bounds materialized in the embedded image. */
static void z_macho_init_array_marker_fn(void)
{
}

void (*const __zephyr_init_array_start[])(void)
	__attribute__((used, visibility("default"),
		       section("__DATA,z_init_array"))) =
		{ z_macho_init_array_marker_fn };

void (*const __zephyr_init_array_end[])(void)
	__attribute__((used, visibility("default"),
		       section("__DATA,z_init_array"))) =
		{ z_macho_init_array_marker_fn };

#define MACHO_INIT_MARKERS(level) \
	static const struct init_entry _CONCAT(__macho_init_range_start_, level) \
	__used __noasan Z_INIT_ENTRY_SECTION(level, 0, 0) = {0}; \
	static const struct init_entry _CONCAT(__macho_init_range_end_, level) \
	__used __noasan Z_INIT_ENTRY_SECTION(level, 0, 0) = {0}

MACHO_INIT_MARKERS(EARLY);
MACHO_INIT_MARKERS(PRE_KERNEL_1);
MACHO_INIT_MARKERS(PRE_KERNEL_2);
MACHO_INIT_MARKERS(POST_KERNEL);
MACHO_INIT_MARKERS(APPLICATION);
MACHO_INIT_MARKERS(SMP);

struct macho_init_range {
	const struct init_entry *start;
	const struct init_entry *end;
};

#define MACHO_INIT_RANGE(level) \
	{ \
		&_CONCAT(__macho_init_range_start_, level), \
		&_CONCAT(__macho_init_range_end_, level), \
	}

void arch_sys_init_run_level(unsigned int level)
{
	static const struct macho_init_range ranges[] = {
		MACHO_INIT_RANGE(EARLY),
		MACHO_INIT_RANGE(PRE_KERNEL_1),
		MACHO_INIT_RANGE(PRE_KERNEL_2),
		MACHO_INIT_RANGE(POST_KERNEL),
		MACHO_INIT_RANGE(APPLICATION),
		MACHO_INIT_RANGE(SMP),
	};
	const struct init_entry *entry;

	if (level >= ARRAY_SIZE(ranges)) {
		return;
	}

	for (entry = ranges[level].start; entry < ranges[level].end; entry++) {
		if ((entry->init_fn != NULL) || (entry->dev != NULL)) {
			z_sys_init_run_entry(entry, level);
		}
	}
}

void arch_static_init_gnu(void)
{
	void (*const *fn)(void);

	for (fn = __zephyr_init_array_start;
	     fn != __zephyr_init_array_end; fn++) {
		(*fn)();
	}
}

#endif /* __APPLE__ */
