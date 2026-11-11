/********************************************************************************************************
 * @file    zcl_powerCfg_attr.c
 *
 * @brief   This is the source file for zcl_powerCfg_attr
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
#if 0
#include "zb_common.h"
#include "../zcl_include.h"

#ifdef ZCL_POWER

uint16_t zcl_power_config_attr_MainsVoltage;
uint8_t zcl_power_config_attr_MainsFrequency;
uint8_t zcl_power_config_attr_MainsAlarmMask;
uint16_t zcl_power_config_attr_MainsVoltageMinThreshold;
uint16_t zcl_power_config_attr_MainsVoltageMaxThreshold = 0xffff;
uint16_t zcl_power_config_attr_MainsVoltageDwellTripPoint;
uint8_t zcl_power_config_attr_BatteryVoltage;
uint8_t zcl_power_config_attr_BatteryPercentageRemaining;
uint8_t zcl_power_config_attr_BatteryManufacturer[ZCL_BATTERYMANUFACTURER_MAX_LENGTH + 1];
uint8_t zcl_power_config_attr_BatterySize  = 0xff;
uint16_t zcl_power_config_attr_BatteryAHrRating;
uint8_t zcl_power_config_attr_BatteryQuantity;
uint8_t zcl_power_config_attr_BatteryRatedVoltage;
uint8_t zcl_power_config_attr_BatteryAlarmMask;
uint8_t zcl_power_config_attr_BatteryVoltageMinThreshold;
uint8_t zcl_power_config_attr_BatteryVoltageThreshold1;
uint8_t zcl_power_config_attr_BatteryVoltageThreshold2;
uint8_t zcl_power_config_attr_BatteryVoltageThreshold3;
uint8_t zcl_power_config_attr_BatteryPercentageMinThreshold;
uint8_t zcl_power_config_attr_BatteryPercentageThreshold1;
uint8_t zcl_power_config_attr_BatteryPercentageThreshold2;
uint8_t zcl_power_config_attr_BatteryPercentageThreshold3;
uint32_t zcl_power_config_attr_BatteryAlarmState;


const zclAttrInfo_t power_config_attrTbl[] = {
    {ZCL_POWER_CONFIG_ATTRID_MAINSVOLTAGE,                  ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ,                             (uint8_t*)&zcl_power_config_attr_MainsVoltage},
    {ZCL_POWER_CONFIG_ATTRID_MAINSFREQUENCY,                ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,                             (uint8_t*)&zcl_power_config_attr_MainsFrequency},
    {ZCL_POWER_CONFIG_ATTRID_MAINSALARMMASK,                ZCL_DATA_TYPE_BITMAP8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_MainsAlarmMask},
    {ZCL_POWER_CONFIG_ATTRID_MAINSVOLTAGEMINTHRESHOLD,      ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_MainsVoltageMinThreshold},
    {ZCL_POWER_CONFIG_ATTRID_MAINSVOLTAGEMAXTHRESHOLD,      ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_MainsVoltageMaxThreshold},
    {ZCL_POWER_CONFIG_ATTRID_MAINSVOLTAGEDWELLTRIPPOINT,    ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_MainsVoltageDwellTripPoint},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYVOLTAGE,                ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,                             (uint8_t*)&zcl_power_config_attr_BatteryVoltage},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYPERCENTAGEREMAINING,    ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (uint8_t*)&zcl_power_config_attr_BatteryPercentageRemaining},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYMANUFACTURER,           ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      zcl_power_config_attr_BatteryManufacturer},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYSIZE,                   ZCL_DATA_TYPE_ENUM8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatterySize},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYAHRRATING,              ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryAHrRating},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYQUANTITY,               ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryQuantity},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYRATEDVOLTAGE,           ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryRatedVoltage},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYALARMMASK,              ZCL_DATA_TYPE_BITMAP8,  ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryAlarmMask},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYVOLTAGEMINTHRESHOLD,    ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryVoltageMinThreshold},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYVOLTAGETHRESHOLD1,      ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryVoltageThreshold1},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYVOLTAGETHRESHOLD2,      ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryVoltageThreshold2},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYVOLTAGETHRESHOLD3,      ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryVoltageThreshold3},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYPERCENTAGEMINTHRESHOLD, ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryPercentageMinThreshold},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYPERCENTAGETHRESHOLD1,   ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryPercentageThreshold1},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYPERCENTAGETHRESHOLD2,   ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryPercentageThreshold2},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYPERCENTAGETHRESHOLD3,   ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,      (uint8_t*)&zcl_power_config_attr_BatteryPercentageThreshold3},
    {ZCL_POWER_CONFIG_ATTRID_BATTERYALARMSTATE,             ZCL_DATA_TYPE_BITMAP32, ACCESS_CONTROL_READ,                             (uint8_t*)&zcl_power_config_attr_BatteryAlarmState},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION,                    ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ,                             (uint8_t*)&zcl_attr_global_clusterRevision},
};

const uint8_t zcl_powerConfig_attrNum = (sizeof(power_config_attrTbl) / sizeof(zclAttrInfo_t));

#endif

#endif
