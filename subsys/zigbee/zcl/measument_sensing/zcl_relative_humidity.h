/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZCL_RELATIVE_HUMIDITY_H
#define ZCL_RELATIVE_HUMIDITY_H

/* Relative Humidity Measurement cluster (0x0405) attribute IDs */
#define ZCL_RELATIVE_HUMIDITY_ATTRID_MEASUREDVALUE      0x0000
#define ZCL_RELATIVE_HUMIDITY_ATTRID_MINMEASUREDVALUE   0x0001
#define ZCL_RELATIVE_HUMIDITY_ATTRID_MAXMEASUREDVALUE   0x0002
#define ZCL_RELATIVE_HUMIDITY_ATTRID_TOLERANCE          0x0003

status_t zcl_relative_humidity_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum,
					const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

#endif /* ZCL_RELATIVE_HUMIDITY_H */
