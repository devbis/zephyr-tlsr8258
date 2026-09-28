/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stands in for the vendor tl_common.h, which pulls in the chip_8258
 * platform. Imported sources that include "../tl_common.h" get the pieces of
 * it this port provides.
 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_TL_COMMON_H_
#define ZEPHYR_SUBSYS_ZIGBEE_TL_COMMON_H_

#include "tl_platform.h"
#include "ev_buffer.h"
#include "ev_queue.h"
#include "drv_hw.h"

#endif /* ZEPHYR_SUBSYS_ZIGBEE_TL_COMMON_H_ */
