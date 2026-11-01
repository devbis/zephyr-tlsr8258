/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../zcl_include.h"

#ifdef ZCL_FAN_CONTROL

_CODE_ZCL_ status_t zcl_fan_control_register(u8 endpoint, u16 manuCode, u8 attrNum,
					     const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
	return zcl_registerCluster(endpoint, ZCL_CLUSTER_HAVC_FAN_CONTROL, manuCode, attrNum,
				   attrTbl, NULL, cb);
}

#endif /* ZCL_FAN_CONTROL */
