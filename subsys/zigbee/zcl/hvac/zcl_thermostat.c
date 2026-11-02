/********************************************************************************************************
 * @file    zcl_thermostat.c
 *
 * @brief   This is the source file for zcl_thermostat
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

/**********************************************************************
 * INCLUDES
 */
#include "../zcl_include.h"

#ifdef ZCL_THERMOSTAT
/**********************************************************************
 * LOCAL CONSTANTS
 */


/**********************************************************************
 * LOCAL TYPES
 */


/**********************************************************************
 * LOCAL VARIABLES
 */


/**********************************************************************
 * LOCAL FUNCTIONS
 */
static status_t zcl_thermostat_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_thermostat_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    return zcl_registerCluster(endpoint, ZCL_CLUSTER_HVAC_THERMOSTAT, manuCode, attrNum, attrTbl, zcl_thermostat_cmdHandler, cb);
}

_CODE_ZCL_ status_t zcl_thermostat_setpointRaiseLowerCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_thermostat_setpointRaiseLowerCmd_t *pCmd)
{
    uint8_t buf[2];
    uint8_t *pBuf = buf;

    *pBuf++ = pCmd->mode;
    *pBuf++ = pCmd->amount;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_HVAC_THERMOSTAT, ZCL_CMD_THERMOSTAT_SETPOINT_RAISE_LOWER, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, buf);
}

_CODE_ZCL_ status_t zcl_thermostat_setWeeklyScheduleCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_thermostat_setWeeklyScheduleCmd_t *pCmd)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t len = 3;

    if ((pCmd->modeForSequence == HEAT_SETPOINT_FIELD_PRESENT) ||
        (pCmd->modeForSequence == COOL_SETPOINT_FIELD_PRESENT)) {
        len += pCmd->numOfTransForSequence * 4;
    } else if (pCmd->modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
        len += pCmd->numOfTransForSequence * 6;
    } else {
        return ZCL_STA_FAILURE;
    }

    uint8_t *buf = (uint8_t *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = pCmd->numOfTransForSequence;
    *pBuf++ = pCmd->dayOfWeekForSequence;
    *pBuf++ = pCmd->modeForSequence;
    for (uint8_t i = 0; i < pCmd->numOfTransForSequence; i++) {
        if (pCmd->modeForSequence == HEAT_SETPOINT_FIELD_PRESENT) {
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pHeatMode[i].transTime);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pHeatMode[i].transTime);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pHeatMode[i].heatSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pHeatMode[i].heatSetpoint);
        } else if (pCmd->modeForSequence == COOL_SETPOINT_FIELD_PRESENT) {
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pCoolMode[i].transTime);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pCoolMode[i].transTime);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pCoolMode[i].coolSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pCoolMode[i].coolSetpoint);
        } else if (pCmd->modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pBothMode[i].transTime);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pBothMode[i].transTime);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pBothMode[i].heatSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pBothMode[i].heatSetpoint);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pBothMode[i].coolSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pBothMode[i].coolSetpoint);
        } else {
            status = ZCL_STA_FAILURE;
        }
    }

    if (status != ZCL_STA_FAILURE) {
        status = zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_HVAC_THERMOSTAT, ZCL_CMD_THERMOSTAT_SET_WEEKLY_SCHEDULE, TRUE,
                             ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, len, buf);
    }

    ev_buf_free(buf);

    return status;
}

_CODE_ZCL_ status_t zcl_thermostat_getWeeklyScheduleCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_thermostat_getWeeklyScheduleCmd_t *pCmd)
{
    uint8_t buf[2];
    uint8_t *pBuf = buf;

    *pBuf++ = pCmd->daysToReturn;
    *pBuf++ = pCmd->modeToReturn;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_HVAC_THERMOSTAT, ZCL_CMD_THERMOSTAT_GET_WEEKLY_SCHEDULE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, buf);
}

_CODE_ZCL_ status_t zcl_thermostat_clearWeeklyScheduleCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_HVAC_THERMOSTAT, ZCL_CMD_THERMOSTAT_CLEAR_WEEKLY_SCHEDULE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_thermostat_getRelayStatusLogCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_HVAC_THERMOSTAT, ZCL_CMD_THERMOSTAT_GET_RELAY_STATUS_LOG, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_thermostat_getWeeklyScheduleRspCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_thermostat_getWeeklyScheduleRspCmd_t *pCmd)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t len = 3;

    if ((pCmd->modeForSequence == HEAT_SETPOINT_FIELD_PRESENT) ||
        (pCmd->modeForSequence == COOL_SETPOINT_FIELD_PRESENT)) {
        len += pCmd->numOfTransForSequence * 4;
    } else if (pCmd->modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
        len += pCmd->numOfTransForSequence * 6;
    } else {
        return ZCL_STA_FAILURE;
    }

    uint8_t *buf = (uint8_t *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = pCmd->numOfTransForSequence;
    *pBuf++ = pCmd->dayOfWeekForSequence;
    *pBuf++ = pCmd->modeForSequence;
    for (uint8_t i = 0; i < pCmd->numOfTransForSequence; i++) {
        if (pCmd->modeForSequence == HEAT_SETPOINT_FIELD_PRESENT) {
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pHeatMode[i].transTime);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pHeatMode[i].transTime);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pHeatMode[i].heatSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pHeatMode[i].heatSetpoint);
        } else if (pCmd->modeForSequence == COOL_SETPOINT_FIELD_PRESENT) {
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pCoolMode[i].transTime);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pCoolMode[i].transTime);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pCoolMode[i].coolSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pCoolMode[i].coolSetpoint);
        } else if (pCmd->modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pBothMode[i].transTime);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pBothMode[i].transTime);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pBothMode[i].heatSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pBothMode[i].heatSetpoint);
            *pBuf++ = LO_UINT16(pCmd->sequenceMode.pBothMode[i].coolSetpoint);
            *pBuf++ = HI_UINT16(pCmd->sequenceMode.pBothMode[i].coolSetpoint);
        } else {
            status = ZCL_STA_FAILURE;
        }
    }

    if (status != ZCL_STA_FAILURE) {
        status = zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_HVAC_THERMOSTAT, ZCL_CMD_THERMOSTAT_GET_WEEKLY_SCHEDULE_RSP, TRUE,
                             ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, len, buf);
    }

    ev_buf_free(buf);

    return status;
}

_CODE_ZCL_ status_t zcl_thermostat_getRelayStatusLogRspCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_thermostat_getRelayStatusLogRspCmd_t *pCmd)
{
    uint8_t buf[11];
    uint8_t *pBuf = buf;

    *pBuf++ = LO_UINT16(pCmd->timeOfDay);
    *pBuf++ = HI_UINT16(pCmd->timeOfDay);
    *pBuf++ = LO_UINT16(pCmd->relayStatus);
    *pBuf++ = HI_UINT16(pCmd->relayStatus);
    *pBuf++ = LO_UINT16(pCmd->localTemp);
    *pBuf++ = HI_UINT16(pCmd->localTemp);
    *pBuf++ = pCmd->hunidityInPercentage;
    *pBuf++ = LO_UINT16(pCmd->setpoint);
    *pBuf++ = HI_UINT16(pCmd->setpoint);
    *pBuf++ = LO_UINT16(pCmd->unreadEntries);
    *pBuf++ = HI_UINT16(pCmd->unreadEntries);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_HVAC_THERMOSTAT, ZCL_CMD_THERMOSTAT_GET_RELAY_STATUS_LOG_RSP, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 11, buf);
}



_CODE_ZCL_ static status_t zcl_thermostat_setpointRaiseLowerPrc(zclIncoming_t *pInMsg)
{
    status_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_thermostat_setpointRaiseLowerCmd_t cmd;
        ZB_SETSTRUCTCONTENT(cmd, 0);

        cmd.mode = *pData++;
        cmd.amount = *pData++;

        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_setWeeklySchedulePrc(zclIncoming_t *pInMsg)
{
    status_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_thermostat_setWeeklyScheduleCmd_t cmd;
        ZB_SETSTRUCTCONTENT(cmd, 0);

        cmd.numOfTransForSequence = *pData++;
        cmd.dayOfWeekForSequence = *pData++;
        cmd.modeForSequence = *pData++;
        if (cmd.numOfTransForSequence) {
            if (cmd.modeForSequence == HEAT_SETPOINT_FIELD_PRESENT) {
                cmd.sequenceMode.pHeatMode = (heatMode_t *)pData;
            } else if (cmd.modeForSequence == COOL_SETPOINT_FIELD_PRESENT) {
                cmd.sequenceMode.pCoolMode = (coolMode_t *)pData;
            } else if (cmd.modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
                cmd.sequenceMode.pBothMode = (bothMode_t *)pData;
            }
        }

        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_getWeeklySchedulePrc(zclIncoming_t *pInMsg)
{
    status_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_thermostat_getWeeklyScheduleCmd_t cmd;
        ZB_SETSTRUCTCONTENT(cmd, 0);

        cmd.daysToReturn = *pData++;
        cmd.modeToReturn = *pData++;

        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_clearWeeklySchedulePrc(zclIncoming_t *pInMsg)
{
    status_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_getRelayStatusLogPrc(zclIncoming_t *pInMsg)
{
    status_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        //no payload.

        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_getWeeklyScheduleRspPrc(zclIncoming_t *pInMsg)
{
    status_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_thermostat_getWeeklyScheduleRspCmd_t cmd;
        ZB_SETSTRUCTCONTENT(cmd, 0);

        cmd.numOfTransForSequence = *pData++;
        cmd.dayOfWeekForSequence = *pData++;
        cmd.modeForSequence = *pData++;
        if (cmd.numOfTransForSequence) {
            if (cmd.modeForSequence == HEAT_SETPOINT_FIELD_PRESENT) {
                cmd.sequenceMode.pHeatMode = (heatMode_t *)pData;
            } else if (cmd.modeForSequence == COOL_SETPOINT_FIELD_PRESENT) {
                cmd.sequenceMode.pCoolMode = (coolMode_t *)pData;
            } else if (cmd.modeForSequence == BOTH_SETPOINT_FIELD_PRESENT) {
                cmd.sequenceMode.pBothMode = (bothMode_t *)pData;
            }
        }

        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_getRelayStatusLogRspPrc(zclIncoming_t *pInMsg)
{
    status_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_thermostat_getRelayStatusLogRspCmd_t cmd;
        ZB_SETSTRUCTCONTENT(cmd, 0);

        cmd.timeOfDay = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmd.relayStatus = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmd.localTemp = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmd.hunidityInPercentage = *pData++;
        cmd.setpoint = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmd.unreadEntries = BUILD_U16(pData[0], pData[1]);
        pData += 2;

        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_THERMOSTAT_SETPOINT_RAISE_LOWER:
        status = zcl_thermostat_setpointRaiseLowerPrc(pInMsg);
        break;
    case ZCL_CMD_THERMOSTAT_SET_WEEKLY_SCHEDULE:
        status = zcl_thermostat_setWeeklySchedulePrc(pInMsg);
        break;
    case ZCL_CMD_THERMOSTAT_GET_WEEKLY_SCHEDULE:
        status = zcl_thermostat_getWeeklySchedulePrc(pInMsg);
        break;
    case ZCL_CMD_THERMOSTAT_CLEAR_WEEKLY_SCHEDULE:
        status = zcl_thermostat_clearWeeklySchedulePrc(pInMsg);
        break;
    case ZCL_CMD_THERMOSTAT_GET_RELAY_STATUS_LOG:
        status = zcl_thermostat_getRelayStatusLogPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_serverCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_THERMOSTAT_GET_WEEKLY_SCHEDULE_RSP:
        status = zcl_thermostat_getWeeklyScheduleRspPrc(pInMsg);
        break;
    case ZCL_CMD_THERMOSTAT_GET_RELAY_STATUS_LOG_RSP:
        status = zcl_thermostat_getRelayStatusLogRspPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_thermostat_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_thermostat_clientCmdHandler(pInMsg);
    } else {
        return zcl_thermostat_serverCmdHandler(pInMsg);
    }
}

#endif	/* ZCL_THERMOSTAT */
