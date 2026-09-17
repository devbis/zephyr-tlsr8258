/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __APPLE__

#include <stddef.h>
#include <string.h>

#include "nsi_hw_scheduler_backend.h"
#include "nsi_tracing.h"

extern struct nsi_hw_event_st __nsi_hw_events_start[];
extern struct nsi_hw_event_st __nsi_hw_events_end[];

struct nsi_hw_event_st nsi_hws_events[NSI_HWS_MAX_EVENTS];
unsigned int nsi_hws_backend_event_count;

void nsi_hws_backend_init(void)
{
	size_t event_count = (size_t)(__nsi_hw_events_end - __nsi_hw_events_start);

	if (event_count > NSI_HWS_MAX_EVENTS) {
		nsi_print_error_and_exit("Too many NSI HW events in Mach-O image\n");
	}

	for (size_t i = 0U; i < event_count; i++) {
		struct nsi_hw_event_st event = { .callback = NULL };
		size_t j = i;

		memcpy(&event, &__nsi_hw_events_start[i], sizeof(event));

		while ((j > 0U) &&
		       (nsi_hws_events[j - 1U].priority > event.priority)) {
			memcpy(&nsi_hws_events[j], &nsi_hws_events[j - 1U], sizeof(event));
			j--;
		}

		memcpy(&nsi_hws_events[j], &event, sizeof(event));
	}

	nsi_hws_backend_event_count = (unsigned int)event_count;
}

#endif /* __APPLE__ */
