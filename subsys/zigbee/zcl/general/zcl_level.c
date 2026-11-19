/********************************************************************************************************
 * @file    zcl_level.c
 *
 * @brief   This is the source file for zcl_level
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


#ifdef ZCL_LEVEL_CTRL
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
static void zcl_level_startUpCurrentLevel(uint8_t endpoint);
static status_t zcl_level_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_level_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    uint8_t status = ZCL_STA_SUCCESS;

    status = zcl_registerCluster(endpoint, ZCL_CLUSTER_GEN_LEVEL_CONTROL, manuCode, attrNum, attrTbl, zcl_level_cmdHandler, cb);

    if (status == ZCL_STA_SUCCESS) {
    	zcl_level_startUpCurrentLevel(endpoint);
    }

    return status;
}

_CODE_ZCL_ static void zcl_level_startUpCurrentLevel(uint8_t endpoint)
{
    uint8_t startUpCurrentLevel = 0;
    uint8_t preLevel = 0;
    uint16_t attrLen = 0;

    if ((zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_LEVEL_CONTROL, ZCL_ATTRID_LEVEL_START_UP_CURRENT_LEVEL, &attrLen, (uint8_t *)&startUpCurrentLevel) == ZCL_STA_SUCCESS) &&
        (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_LEVEL_CONTROL, ZCL_ATTRID_LEVEL_CURRENT_LEVEL, &attrLen, (uint8_t *)&preLevel) == ZCL_STA_SUCCESS)) {
        uint8_t dstLevel = 0;

        if (startUpCurrentLevel == ZCL_START_UP_CURRENT_LEVEL_TO_MIN) {
            dstLevel = ZCL_LEVEL_ATTR_MIN_LEVEL;
        } else if (startUpCurrentLevel == ZCL_START_UP_CURRENT_LEVEL_TO_PREVIOUS) {
            dstLevel = preLevel;
        } else {
            dstLevel = startUpCurrentLevel;
        }

        if (dstLevel != preLevel) {
            zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_LEVEL_CONTROL, ZCL_ATTRID_LEVEL_CURRENT_LEVEL, (uint8_t *)&dstLevel);
        }
    }
}

_CODE_ZCL_ status_t zcl_level_move2level(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, moveToLvl_t *pMove2Level)
{
    uint8_t buf[5];
    uint8_t *pBuf = buf;

    *pBuf++ = pMove2Level->level;
    *pBuf++ = LO_UINT16(pMove2Level->transitionTime);
    *pBuf++ = HI_UINT16(pMove2Level->transitionTime);
    if (pMove2Level->optPresent) {
        *pBuf++ = pMove2Level->optionsMask;
        *pBuf++ = pMove2Level->optionsOverride;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_LEVEL_CONTROL, cmdId, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_level_move(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, move_t *pMove)
{
    uint8_t buf[4];
    uint8_t *pBuf = buf;

    *pBuf++ = pMove->moveMode;
    *pBuf++ = pMove->rate;
    if (pMove->optPresent) {
        *pBuf++ = pMove->optionsMask;
        *pBuf++ = pMove->optionsOverride;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_LEVEL_CONTROL, cmdId, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_level_step(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, step_t *pStep)
{
    uint8_t buf[6];
    uint8_t *pBuf = buf;

    *pBuf++ = pStep->stepMode;
    *pBuf++ = pStep->stepSize;
    *pBuf++ = LO_UINT16(pStep->transitionTime);
    *pBuf++ = HI_UINT16(pStep->transitionTime);
    if (pStep->optPresent) {
        *pBuf++ = pStep->optionsMask;
        *pBuf++ = pStep->optionsOverride;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_LEVEL_CONTROL, cmdId, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_level_stop(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, stop_t *pStop)
{
    uint8_t buf[2];
    uint8_t *pBuf = buf;

    if (pStop->optPresent) {
        *pBuf++ = pStop->optionsMask;
        *pBuf++ = pStop->optionsOverride;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_LEVEL_CONTROL, cmdId, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ static status_t zcl_level_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    uint8_t optionsMask = 0;
    uint8_t optionsOverride = 0;

    zcl_level_cmdPayload_t cmdPayload;
    memset((uint8_t *)&cmdPayload, 0, sizeof(zcl_level_cmdPayload_t));

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:
        cmdPayload.moveToLevel.level = *pData++;
        cmdPayload.moveToLevel.transitionTime = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        if (pInMsg->dataLen >= 5) {
            cmdPayload.moveToLevel.optionsMask = *pData++;
            cmdPayload.moveToLevel.optionsOverride = *pData++;
            cmdPayload.moveToLevel.optPresent = 1;

            optionsMask = cmdPayload.moveToLevel.optionsMask;
            optionsOverride = cmdPayload.moveToLevel.optionsOverride;
        }
        break;
    case ZCL_CMD_LEVEL_MOVE:
    case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF:
        cmdPayload.move.moveMode = *pData++;
        cmdPayload.move.rate = *pData++;
        if (pInMsg->dataLen >= 4) {
            cmdPayload.move.optionsMask = *pData++;
            cmdPayload.move.optionsOverride = *pData++;
            cmdPayload.move.optPresent = 1;

            optionsMask = cmdPayload.move.optionsMask;
            optionsOverride = cmdPayload.move.optionsOverride;
        }
        break;
    case ZCL_CMD_LEVEL_STEP:
    case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF:
        cmdPayload.step.stepMode = *pData++;
        cmdPayload.step.stepSize = *pData++;
        cmdPayload.step.transitionTime = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        if (pInMsg->dataLen >= 6) {
            cmdPayload.step.optionsMask = *pData++;
            cmdPayload.step.optionsOverride = *pData++;
            cmdPayload.step.optPresent = 1;

            optionsMask = cmdPayload.step.optionsMask;
            optionsOverride = cmdPayload.step.optionsOverride;
        }
        break;
    case ZCL_CMD_LEVEL_STOP:
    case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:
        if (pInMsg->dataLen >= 2) {
            cmdPayload.stop.optionsMask = *pData++;
            cmdPayload.stop.optionsOverride = *pData++;
            cmdPayload.stop.optPresent = 1;

            optionsMask = cmdPayload.stop.optionsMask;
            optionsOverride = cmdPayload.stop.optionsOverride;
        }
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    if (status == ZCL_STA_SUCCESS) {
        bool execute = FALSE;
        uint16_t attrLen = 0;

        bool onOff = FALSE;
        zcl_getAttrVal(pInMsg->msg->indInfo.dst_ep, ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ONOFF, &attrLen, (uint8_t *)&onOff);

        uint8_t options = 0;
        zcl_getAttrVal(pInMsg->msg->indInfo.dst_ep, ZCL_CLUSTER_GEN_LEVEL_CONTROL, ZCL_ATTRID_LEVEL_OPTIONS, &attrLen, (uint8_t *)&options);

        if (onOff || ((pInMsg->hdr.cmd >= ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF) && (pInMsg->hdr.cmd <= ZCL_CMD_LEVEL_STOP_WITH_ON_OFF))) {
            execute = TRUE;
        } else if (!(options & ZCL_LEVEL_OPTIONS_EXECUTE_IF_OFF)) {
            if (optionsMask && ((optionsMask & optionsOverride) == ZCL_LEVEL_OPTIONS_EXECUTE_IF_OFF)) {
                execute = TRUE;
            }
        } else if (options & ZCL_LEVEL_OPTIONS_EXECUTE_IF_OFF) {
            if ((optionsMask == 0) || (optionsMask & optionsOverride) == ZCL_LEVEL_OPTIONS_EXECUTE_IF_OFF) {
                execute = TRUE;
            }
        }

        if (execute && pInMsg->clusterAppCb) {
            pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmdPayload);

#ifdef ZCL_SCENE
            uint16_t sceneAttrLen = 0;
            uint8_t sceneValid = 0;
            if (zcl_getAttrVal(pInMsg->msg->indInfo.dst_ep, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_VALID, &sceneAttrLen, (uint8_t *)&sceneValid) == ZCL_STA_SUCCESS) {
                sceneValid = 0;
                zcl_setAttrVal(pInMsg->msg->indInfo.dst_ep, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_VALID, (uint8_t *)&sceneValid);
            }
#endif
        }
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_level_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_level_clientCmdHandler(pInMsg);
    } else {
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    }
}

#endif  /* ZCL_LEVEL_CTRL */
