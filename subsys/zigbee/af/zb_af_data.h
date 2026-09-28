/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZB_AF_DATA_H
#define _ZB_AF_DATA_H

#include "zb_common.h"

void af_dataCnfHandler(void *arg);
void af_aps_data_entry(void *arg);
void af_aps_data_fragment_entry(void *arg);

#endif
