/* SPDX-License-Identifier: Apache-2.0 */
/* Zephyr guard for the deferred MLME-ASSOCIATE.confirm completion race. */
#ifndef ZDO_JOIN_CONFIRM_GUARD_H
#define ZDO_JOIN_CONFIRM_GUARD_H

#include <stdbool.h>

/*
 * A successful synthetic completion may move the manager to IDLE before the
 * real deferred ASSOCIATE.confirm is delivered.  Keep the late-success
 * recovery available for an unhandled cycle, but do not re-run completion
 * callbacks for a cycle that has already completed successfully.
 */
static inline bool zdo_join_confirm_is_duplicate(bool completion_handled,
						 bool active_join_state,
						 bool success)
{
	return completion_handled && !active_join_state && success;
}

#endif /* ZDO_JOIN_CONFIRM_GUARD_H */
