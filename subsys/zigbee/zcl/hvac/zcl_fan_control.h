/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZCL_FAN_CONTROL_H
#define ZCL_FAN_CONTROL_H

/* Fan Control cluster (0x0202) attribute IDs */
#define ZCL_ATTRID_HVAC_FAN_CONTROL_FAN_MODE            0x0000
#define ZCL_ATTRID_HVAC_FAN_CONTROL_FAN_MODE_SEQUENCE   0x0001

/* FanMode */
#define ZCL_FAN_MODE_OFF        0x00
#define ZCL_FAN_MODE_LOW        0x01
#define ZCL_FAN_MODE_MEDIUM     0x02
#define ZCL_FAN_MODE_HIGH       0x03
#define ZCL_FAN_MODE_ON         0x04
#define ZCL_FAN_MODE_AUTO       0x05
#define ZCL_FAN_MODE_SMART      0x06

/* FanModeSequence */
#define ZCL_FAN_MODE_SEQ_LOW_MED_HIGH       0x00
#define ZCL_FAN_MODE_SEQ_LOW_HIGH           0x01
#define ZCL_FAN_MODE_SEQ_LOW_MED_HIGH_AUTO  0x02
#define ZCL_FAN_MODE_SEQ_LOW_HIGH_AUTO      0x03
#define ZCL_FAN_MODE_SEQ_ON_AUTO            0x04

status_t zcl_fan_control_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum,
				  const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

#endif /* ZCL_FAN_CONTROL_H */
