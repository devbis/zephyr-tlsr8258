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

extern u32 sysTimerPerUs;

startup_state_e drv_platform_init(void);

void drv_enable_irq(void);
u32 drv_disable_irq(void);
u32 drv_restore_irq(u32 en);

void drv_irqMask_clear(void);

void drv_wd_setInterval(u32 ms);
void drv_wd_start(void);
void drv_wd_clear(void);

u32 drv_u32Rand(void);
void drv_generateRandomData(u8 *pData, u8 len);

void voltage_detect(bool powerOn);
void drv_vbusWatchdogClose(void);

void flash_read(u32 addr, u32 len, u8 *buf);
void flash_write(u32 addr, u32 len, u8 *buf);
void flash_erase(u32 addr);
bool drv_get_primary_ieee_addr(u8 *addr);
