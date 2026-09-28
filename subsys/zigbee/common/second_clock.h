/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SECOND_CLOCK_H
#define _SECOND_CLOCK_H

#include "zb_common.h"

extern u32 g_secondCnt;
int secondClockPeriodic(void *arg);
void secondClockInit(void);
void secondClockStop(void);
void secondClockRun(void);

#endif
