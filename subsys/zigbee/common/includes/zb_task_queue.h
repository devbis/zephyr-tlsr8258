/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Zephyr replacement for the vendor zigbee/common/includes/zb_task_queue.h.
 *
 * The task queue types and entry points live in zb_common.h on this port,
 * because the Zephyr platform layer implements them. Imported sources that
 * include "zb_task_queue.h" directly land here and get the same declarations.
 */
#ifndef ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_TASK_QUEUE_H_
#define ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_TASK_QUEUE_H_

#include "zb_common.h"

#endif /* ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_TASK_QUEUE_H_ */
