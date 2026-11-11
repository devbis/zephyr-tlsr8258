/********************************************************************************************************
 * @file    drv_hw.h
 *
 * @brief   This is the header file for drv_hw
 *
 * @author  Zigbee Group
 * @date    2021
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *          All rights reserved.
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#pragma once

#include <zephyr/zigbee/zb_types.h>
#include <zephyr/sys/reboot.h>

#define BATTERY_SAFETY_THRESHOLD        2200 /* 2.2 V */

/* SYSTEM_RESET() maps to Zephyr reboot for all MCU cores */
#define SYSTEM_RESET()              sys_reboot(SYS_REBOOT_COLD)

typedef enum {
    SYSTEM_BOOT,                //power on or boot
    SYSTEM_DEEP_RETENTION,      //deep with retention back
    SYSTEM_DEEP,                //deep back
} startup_state_e;

extern uint32_t sysTimerPerUs;

startup_state_e drv_platform_init(void);

uint32_t drv_disable_irq(void);
uint32_t drv_restore_irq(uint32_t en);

void drv_irqMask_clear(void);

void drv_wd_setInterval(uint32_t ms);
void drv_wd_start(void);
void drv_wd_clear(void);

uint32_t drv_u32Rand(void);
void drv_generateRandomData(uint8_t *pData, uint8_t len);

void voltage_detect(bool powerOn);
void drv_vbusWatchdogClose(void);

bool drv_get_primary_ieee_addr(uint8_t *addr);

/* True when VDD is high enough for a flash write or erase. */
bool zb_flash_vdd_ok(void);

/*
 * Hardware timer hook used by the MAC. The vendor SDK declares this in its
 * own drv_hw.h; the Zephyr backend in drv_hw_zephyr.c implements it on top
 * of the event timer.
 */
typedef int (*timerCb_t)(void *arg);
int drv_hwTmr_set(uint8_t tmrIdx, uint32_t t_us, timerCb_t func, void *arg);

/*
 * Outgoing frame counter retained across deep sleep. The vendor keeps it in a
 * retention register; this port restores it from NV instead, so waking never
 * rewinds the counter. Declared here because the stack reaches it through the
 * vendor drv_pm.h, which this port does not import.
 */
uint32_t drv_pm_deepSleep_frameCnt_get(void);
