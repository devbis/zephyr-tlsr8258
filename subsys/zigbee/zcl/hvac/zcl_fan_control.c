/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../zcl_include.h"

#ifdef ZCL_FAN_CONTROL

_CODE_ZCL_ status_t zcl_fan_control_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum,
					     const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
	return zcl_registerCluster(endpoint, ZCL_CLUSTER_HVAC_FAN_CONTROL, manuCode, attrNum,
				   attrTbl, NULL, cb);
}

#endif /* ZCL_FAN_CONTROL */
