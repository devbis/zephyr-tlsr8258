/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Zephyr replacement for the vendor zigbee/common/includes/zb_task_queue.h.
 *
 * The task and queue types live in zb_common.h on this port, because the
 * Zephyr platform layer implements the queues on kernel primitives. Imported
 * sources that include "zb_task_queue.h" directly land here and get the same
 * declarations.
 *
 * Implemented by platform/zephyr/zb_task_queue_zephyr.c.
 */
#ifndef ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_TASK_QUEUE_H_
#define ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_TASK_QUEUE_H_

#include "zb_common.h"

#endif /* ZEPHYR_SUBSYS_ZIGBEE_COMMON_INCLUDES_ZB_TASK_QUEUE_H_ */
