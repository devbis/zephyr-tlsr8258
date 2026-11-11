/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZCL_THERMOSTAT_UI_CFG_H
#define ZCL_THERMOSTAT_UI_CFG_H

/* Thermostat User Interface Configuration cluster (0x0204) attribute IDs */
#define ZCL_ATTRID_HVAC_UI_CFG_TEMPERATURE_DISPLAY_MODE 0x0000
#define ZCL_ATTRID_HVAC_UI_CFG_KEYPAD_LOCKOUT           0x0001
#define ZCL_ATTRID_HVAC_UI_CFG_SCHEDULE_PROG_VISIBILITY 0x0002

/* TemperatureDisplayMode */
#define ZCL_UI_CFG_DISPLAY_MODE_CELSIUS     0x00
#define ZCL_UI_CFG_DISPLAY_MODE_FAHRENHEIT  0x01

status_t zcl_thermostat_ui_cfg_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum,
					const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

#endif /* ZCL_THERMOSTAT_UI_CFG_H */
