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

#define BATTERY_SAFETY_THRESHOLD        2200//2.2v

#if defined(MCU_CORE_826x) || defined(MCU_CORE_8258) || defined(MCU_CORE_8278)
    #define SYSTEM_RESET()              mcu_reset()
#elif defined(MCU_CORE_B91) || defined(MCU_CORE_B92) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X)
    #define SYSTEM_RESET()              sys_reboot()
#endif

typedef enum {
    SYSTEM_BOOT,                //power on or boot
    SYSTEM_DEEP_RETENTION,      //deep with retention back
    SYSTEM_DEEP,                //deep back
} startup_state_e;

extern uint32_t sysTimerPerUs;

startup_state_e drv_platform_init(void);

void drv_enable_irq(void);
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
