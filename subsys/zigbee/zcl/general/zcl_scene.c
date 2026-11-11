/********************************************************************************************************
 * @file    zcl_scene.c
 *
 * @brief   This is the source file for zcl_scene
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


#ifdef ZCL_SCENE
/**********************************************************************
 * LOCAL CONSTANTS
 */


/**********************************************************************
 * LOCAL TYPES
 */


/**********************************************************************
 * LOCAL VARIABLES
 */
zcl_sceneTable_t g_zcl_sceneTab[ZCL_SCENE_TABLE_NUM];

/**********************************************************************
 * LOCAL FUNCTIONS
 */
void zcl_scene_sceneTabClear(void);
nv_sts_t zcl_scenesRestore(void);
nv_sts_t zcl_scenesSave(uint8_t endpoint);
void zcl_scene_updateSceneCntAttr(uint8_t endpoint);
static status_t zcl_scene_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_scene_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (zcl_scenesRestore() != NV_SUCC) {
    	zcl_scene_sceneTabClear();
    }

    status = zcl_registerCluster(endpoint, ZCL_CLUSTER_GEN_SCENES, manuCode, attrNum, attrTbl, zcl_scene_cmdHandler, cb);

    if (status == ZCL_STA_SUCCESS) {
    	zcl_scene_updateSceneCntAttr(endpoint);
    }

    return status;
}


_CODE_ZCL_ nv_sts_t zcl_scenesSave(uint8_t endpoint)
{
    zcl_scene_updateSceneCntAttr(endpoint);

    return zcl_sceneTable_save();
}

_CODE_ZCL_ nv_sts_t zcl_scenesRestore(void)
{
    return zcl_sceneTable_restore();
}

_CODE_ZCL_ void zcl_scene_clearEntry(zcl_sceneTable_t *pSceneTab)
{
    if (pSceneTab ){
        pSceneTab->used = 0;
        pSceneTab->endpoint = 0xfe;
        memset((uint8_t *)&pSceneTab->scene, 0, sizeof(zcl_sceneEntry_t));
    }
}

_CODE_ZCL_ void zcl_scene_sceneTabClear(void)
{
    for (uint8_t i = 0; i < ZCL_SCENE_TABLE_NUM; i++) {
    	zcl_sceneTable_t *pSceneTab = &g_zcl_sceneTab[i];

    	zcl_scene_clearEntry(pSceneTab);
    }
}

_CODE_ZCL_ zcl_sceneTable_t *zcl_scene_getFreeEntry(void)
{
    for (uint8_t i = 0; i < ZCL_SCENE_TABLE_NUM; i++) {
    	zcl_sceneTable_t *pSceneTab = &g_zcl_sceneTab[i];

        if (!pSceneTab->used) {
            return pSceneTab;
        }
    }
    return NULL;
}

_CODE_ZCL_ zcl_sceneTable_t *zcl_scene_findEntry(uint8_t endpoint, uint16_t groupId, uint8_t sceneId)
{
    for (uint8_t i = 0; i < ZCL_SCENE_TABLE_NUM; i++) {
    	zcl_sceneTable_t *pSceneTab = &g_zcl_sceneTab[i];

        if (pSceneTab->used && (pSceneTab->endpoint == endpoint)) {
            if ((pSceneTab->scene.groupId == groupId) && (pSceneTab->scene.sceneId == sceneId)) {
            	return pSceneTab;
            }
        }
    }
    return NULL;
}

_CODE_ZCL_ uint8_t zcl_scene_getSceneUsedNum(void)
{
    uint8_t cnt = 0;

    for (uint8_t i = 0; i < ZCL_SCENE_TABLE_NUM; i++) {
    	zcl_sceneTable_t *pSceneTab = &g_zcl_sceneTab[i];

    	if (pSceneTab->used) {
            cnt++;
    	}
    }

    return cnt;
}

_CODE_ZCL_ void zcl_scene_updateSceneCntAttr(uint8_t endpoint)
{
    uint16_t attrLen = 0;
    uint8_t sceneCnt = 0;

    if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_COUNT, &attrLen, (uint8_t *)&sceneCnt) == ZCL_STA_SUCCESS) {
        sceneCnt = zcl_scene_getSceneUsedNum();

        zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_COUNT, (uint8_t *)&sceneCnt);
    }
}

_CODE_ZCL_ uint8_t zcl_scene_findAllSceneByGroup(uint8_t endpoint, uint16_t groupId, uint8_t *sceneList)
{
    uint8_t cnt = 0;

    for (uint8_t i = 0; i < ZCL_SCENE_TABLE_NUM; i++) {
        zcl_sceneTable_t *pSceneTab = &g_zcl_sceneTab[i];

        if (pSceneTab->used && (pSceneTab->endpoint == endpoint) && (pSceneTab->scene.groupId == groupId)) {
            sceneList[cnt++] = pSceneTab->scene.sceneId;
        }
    }

    return cnt;
}

_CODE_ZCL_ status_t zcl_scene_addSceneEntry(uint8_t endpoint, zcl_sceneEntry_t *pScene)
{
    zcl_sceneTable_t *pSceneEntry = zcl_scene_findEntry(endpoint, pScene->groupId, pScene->sceneId);
    if (!pSceneEntry) {
        pSceneEntry = zcl_scene_getFreeEntry();
        if (!pSceneEntry) {
            /* scene table full */
            return ZCL_STA_INSUFFICIENT_SPACE;
        }
    }

    /* update or add new */
    pSceneEntry->used = 1;
    pSceneEntry->endpoint = endpoint;
    memcpy((uint8_t *)&pSceneEntry->scene, (uint8_t *)pScene, sizeof(zcl_sceneEntry_t));

    /* scenes save */
    zcl_scenesSave(endpoint);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_scene_removeSceneEntry(uint8_t endpoint, uint16_t groupId, uint8_t sceneId)
{
    zcl_sceneTable_t *pSceneEntry = zcl_scene_findEntry(endpoint, groupId, sceneId);
    if (pSceneEntry) {
        zcl_scene_clearEntry(pSceneEntry);

        /* remove success, update scene table */
        zcl_scenesSave(endpoint);

        return ZCL_STA_SUCCESS;
    } else {
        return ZCL_STA_NOT_FOUND;
    }
}

_CODE_ZCL_ void zcl_scene_removeAllSceneEntry(uint8_t endpoint, uint16_t groupId, bool updateNV)
{
    bool find = FALSE;
    for (uint8_t i = 0; i < ZCL_SCENE_TABLE_NUM; i++) {
        zcl_sceneTable_t *pSceneTab = &g_zcl_sceneTab[i];

        if (pSceneTab->used && (pSceneTab->endpoint == endpoint) && (pSceneTab->scene.groupId == groupId)) {
            zcl_scene_clearEntry(pSceneTab);

            find = TRUE;
        }
    }

    if (find && updateNV) {
        /* remove success, update scene table */
        zcl_scenesSave(endpoint);
    }
}

_CODE_ZCL_ status_t zcl_scene_copySceneEntry(uint8_t endpoint, copyScene_t *pCopyScene)
{
    uint8_t sceneCnt = 0;
    uint8_t sceneList[ZCL_SCENE_TABLE_NUM] = {0};
    zcl_sceneTable_t *pSceneEntry = NULL;
    zcl_sceneTable_t *pSceneToEntry = NULL;

    if (ZB_SCENE_CLUSTER_COPY_SCENE_MODE_ALL(pCopyScene->mode)) {
        sceneCnt = zcl_scene_findAllSceneByGroup(endpoint, pCopyScene->groupIdFrom, sceneList);
        if (sceneCnt == 0) {
            /* scene not found */
            return ZCL_STA_INVALID_FIELD;
        }
    } else {
        pSceneEntry = zcl_scene_findEntry(endpoint, pCopyScene->groupIdFrom, pCopyScene->sceneIdFrom);
        if (pSceneEntry) {
            sceneCnt = 1;
            sceneList[0] = pCopyScene->sceneIdFrom;
        } else {
            /* scene not found */
            return ZCL_STA_INVALID_FIELD;
        }
    }

    uint8_t i = 0;
    uint8_t addCnt = 0;

    for (i = 0; i < sceneCnt; i++) {
        if (zcl_scene_findEntry(endpoint, pCopyScene->groupIdTo, sceneList[i]) == NULL) {
            addCnt++;
        }
    }

    if (addCnt > (ZCL_SCENE_TABLE_NUM - zcl_scene_getSceneUsedNum())) {
        /* space not enough to copy */
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    /* copy scenes */
    for (i = 0; i < sceneCnt; i++) {
        /* find source scene */
        pSceneEntry = zcl_scene_findEntry(endpoint, pCopyScene->groupIdFrom, sceneList[i]);
        if (pSceneEntry) {
            uint8_t addSceneId = ZB_SCENE_CLUSTER_COPY_SCENE_MODE_ALL(pCopyScene->mode) ? sceneList[i] : pCopyScene->sceneIdTo;

            pSceneToEntry = zcl_scene_findEntry(endpoint, pCopyScene->groupIdTo, addSceneId);
            if (!pSceneToEntry) {
                pSceneToEntry = zcl_scene_getFreeEntry();
                if (!pSceneToEntry) {
                    return ZCL_STA_INSUFFICIENT_SPACE;
                }
            }

            /* add or update */
            pSceneToEntry->used = 1;
            pSceneToEntry->endpoint = endpoint;
            memcpy((uint8_t *)&pSceneToEntry->scene, (uint8_t *)&pSceneEntry->scene, sizeof(zcl_sceneEntry_t));
            pSceneToEntry->scene.sceneId = addSceneId;             //re-write scene id
            pSceneToEntry->scene.groupId = pCopyScene->groupIdTo;  //re-write group id
        }
    }

    /* scenes save */
    zcl_scenesSave(endpoint);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_scene_addScene(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, addScene_t *pAddScene)
{
    uint8_t len = 5;//group id + scene id + transition time
    len += (1 + pAddScene->scene.sceneName[0]);
    len += pAddScene->scene.extFieldLen;

    uint8_t *buf = (uint8_t *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = LO_UINT16(pAddScene->scene.groupId);
    *pBuf++ = HI_UINT16(pAddScene->scene.groupId);
    *pBuf++ = pAddScene->scene.sceneId;
    *pBuf++ = LO_UINT16(pAddScene->scene.transTime);
    *pBuf++ = HI_UINT16(pAddScene->scene.transTime);
    *pBuf++ = pAddScene->scene.sceneName[0];
    memcpy(pBuf, &pAddScene->scene.sceneName[1], pAddScene->scene.sceneName[0]);
    pBuf += pAddScene->scene.sceneName[0];
    if (pAddScene->scene.extFieldLen) {
    	memcpy(pBuf, pAddScene->scene.extField, pAddScene->scene.extFieldLen);
    	pBuf += pAddScene->scene.extFieldLen;
    }

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, cmdId, TRUE,
                ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_scene_viewScene(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, viewScene_t *pViewScene)
{
    uint8_t buf[3];

    buf[0] = LO_UINT16(pViewScene->groupId);
    buf[1] = HI_UINT16(pViewScene->groupId);
    buf[2] = pViewScene->sceneId;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, cmdId, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 3, buf);
}

_CODE_ZCL_ status_t zcl_scene_removeScene(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, removeScene_t *pRemoveScene)
{
    uint8_t buf[3];

    buf[0] = LO_UINT16(pRemoveScene->groupId);
    buf[1] = HI_UINT16(pRemoveScene->groupId);
    buf[2] = pRemoveScene->sceneId;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, ZCL_CMD_SCENE_REMOVE_SCENE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 3, buf);
}

_CODE_ZCL_ status_t zcl_scene_removeAllScene(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, removeAllScene_t *pRemoveAllScene)
{
    uint8_t buf[2];

    buf[0] = LO_UINT16(pRemoveAllScene->groupId);
    buf[1] = HI_UINT16(pRemoveAllScene->groupId);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, ZCL_CMD_SCENE_REMOVE_ALL_SCENE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, buf);
}

_CODE_ZCL_ status_t zcl_scene_storeScene(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, storeScene_t *pStoreScene)
{
    uint8_t buf[3];

    buf[0] = LO_UINT16(pStoreScene->groupId);
    buf[1] = HI_UINT16(pStoreScene->groupId);
    buf[2] = pStoreScene->sceneId;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, ZCL_CMD_SCENE_STORE_SCENE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 3, buf);
}

_CODE_ZCL_ status_t zcl_scene_recallScene(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, recallScene_t *pRecallScene)
{
    uint8_t buf[5];

    uint8_t *pBuf = buf;

    *pBuf++ = LO_UINT16(pRecallScene->groupId);
    *pBuf++ = HI_UINT16(pRecallScene->groupId);
    *pBuf++ = pRecallScene->sceneId;
    if (pRecallScene->transTime) {
        *pBuf++ = LO_UINT16(pRecallScene->transTime);
        *pBuf++ = HI_UINT16(pRecallScene->transTime);
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, ZCL_CMD_SCENE_RECALL_SCENE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_scene_getSceneMemship(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getSceneMemship_t *pGetMembership)
{
    uint8_t buf[2];

    buf[0] = LO_UINT16(pGetMembership->groupId);
    buf[1] = HI_UINT16(pGetMembership->groupId);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, ZCL_CMD_SCENE_GET_SCENE_MEMSHIP, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, buf);
}

_CODE_ZCL_ status_t zcl_scene_copyScene(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, copyScene_t *pCopyScene)
{
    uint8_t buf[7];

    /* If source and destination are same, don't send the command */
    if ((pCopyScene->groupIdFrom == pCopyScene->groupIdTo) && (pCopyScene->sceneIdFrom == pCopyScene->sceneIdTo)) {
    	return ZCL_STA_INVALID_FIELD;
    }

    buf[0] = pCopyScene->mode;
    buf[1] = LO_UINT16(pCopyScene->groupIdFrom);
    buf[2] = HI_UINT16(pCopyScene->groupIdFrom);
    buf[3] = pCopyScene->sceneIdFrom;
    buf[4] = LO_UINT16(pCopyScene->groupIdTo);
    buf[5] = HI_UINT16(pCopyScene->groupIdTo);
    buf[6] = pCopyScene->sceneIdTo;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, ZCL_CMD_SCENE_COPY_SCENE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 7, buf);
}

_CODE_ZCL_ status_t zcl_scene_sceneRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, uint8_t status, uint16_t groupId, uint8_t sceneId)
{
    uint8_t buf[4];

    uint8_t *pBuf = buf;

    *pBuf++ = status;
    *pBuf++ = LO_UINT16(groupId);
    *pBuf++ = HI_UINT16(groupId);
    if (cmdId != ZCL_CMD_SCENE_REMOVE_ALL_SCENE_RSP) {
    	*pBuf++ = sceneId;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, cmdId, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_scene_viewSceneRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo,
                                           uint8_t cmdId, uint8_t status, uint16_t groupId, uint8_t sceneId, zcl_sceneEntry_t *pEntry)
{
    uint8_t len = 4;

    if (status == ZCL_STA_SUCCESS) {
        if (!pEntry) {
            return ZCL_STA_FAILURE;
        }

        len += 3;//transTime + scene name 1 byte
        len += pEntry->sceneName[0];
        len += pEntry->extFieldLen;
    }

    uint8_t *buf = (uint8_t *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = status;
    *pBuf++ = LO_UINT16(groupId);
    *pBuf++ = HI_UINT16(groupId);
    *pBuf++ = sceneId;
    if (status == ZCL_STA_SUCCESS) {
        uint16_t transTime = pEntry->transTime;
        if (cmdId == ZCL_CMD_SCENE_ENHANCED_VIEW_SCENE_RSP) {
            transTime *= 10;
            transTime += pEntry->transTime100ms;
        }
        *pBuf++ = LO_UINT16(transTime);
        *pBuf++ = HI_UINT16(transTime);
        *pBuf++ = pEntry->sceneName[0];
        memcpy(pBuf, &pEntry->sceneName[1], pEntry->sceneName[0]);
        pBuf += pEntry->sceneName[0];
        memcpy(pBuf, pEntry->extField, pEntry->extFieldLen);
        pBuf += pEntry->extFieldLen;
    }

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, cmdId, TRUE,
                ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_scene_getSceneMembershipRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getSceneMemRsp_t *pGetSceneMembershipRsp)
{
    uint8_t len = 4;

    if (pGetSceneMembershipRsp->status == ZCL_STA_SUCCESS) {
        len++;//scene count;
        len += pGetSceneMembershipRsp->sceneCnt;
    }

    uint8_t *buf = (uint8_t *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = pGetSceneMembershipRsp->status;
    *pBuf++ = pGetSceneMembershipRsp->capacity;
    *pBuf++ = LO_UINT16(pGetSceneMembershipRsp->groupId);
    *pBuf++ = HI_UINT16(pGetSceneMembershipRsp->groupId);
    if (pGetSceneMembershipRsp->status == ZCL_STA_SUCCESS) {
        *pBuf++ = pGetSceneMembershipRsp->sceneCnt;
        memcpy(pBuf, pGetSceneMembershipRsp->sceneList, pGetSceneMembershipRsp->sceneCnt);
        pBuf += pGetSceneMembershipRsp->sceneCnt;
    }

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_SCENES, ZCL_CMD_SCENE_GET_SCENE_MEMSHIP_RSP, TRUE,
                ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ static void zcl_addSceneParse(uint8_t endpoint, uint8_t *pData, uint16_t dataLen, addScene_t *pCmd)
{
    uint8_t *ptr = pData;

    pCmd->scene.groupId = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    pCmd->scene.sceneId = *pData++;
    pCmd->scene.transTime = BUILD_U16(pData[0], pData[1]);
    pData += 2;

    uint8_t nameLen = *pData++;

    bool nameSupport = FALSE;
    uint16_t attrLen = 0;
    zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_NAME_SUPPORT, &attrLen, (uint8_t *)&nameSupport);

    if (nameSupport) {
        pCmd->scene.sceneName[0] = (nameLen > (ZCL_MAX_SCENE_NAME_LEN - 1)) ? (ZCL_MAX_SCENE_NAME_LEN - 1) : nameLen;
        memcpy(&pCmd->scene.sceneName[1], pData, pCmd->scene.sceneName[0]);
    }

    pData += nameLen;

    pCmd->scene.extFieldLen = dataLen - (pData - ptr);
    if (pCmd->scene.extFieldLen) {
        pCmd->scene.extFieldLen = (pCmd->scene.extFieldLen > ZCL_MAX_SCENE_EXT_FIELD_SIZE) ? ZCL_MAX_SCENE_EXT_FIELD_SIZE
                                                                                           : pCmd->scene.extFieldLen;
        memcpy(pCmd->scene.extField, pData, pCmd->scene.extFieldLen);
    }
}

_CODE_ZCL_ static void zcl_viewSceneRspParse(uint8_t *pData, uint16_t dataLen, viewSceneRsp_t *pCmd)
{
    uint8_t *ptr = pData;

    pCmd->status = *pData++;
    pCmd->scene.groupId = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    pCmd->scene.sceneId = *pData++;
    if (pCmd->status == ZCL_STA_SUCCESS) {
        pCmd->scene.transTime = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        uint8_t nameLen = *pData++;
        pCmd->scene.sceneName[0] = (nameLen > (ZCL_MAX_SCENE_NAME_LEN - 1)) ? (ZCL_MAX_SCENE_NAME_LEN - 1) : nameLen;
        memcpy(&pCmd->scene.sceneName[1], pData, pCmd->scene.sceneName[0]);

        pData += nameLen;

        pCmd->scene.extFieldLen = dataLen - (pData - ptr);
        if (pCmd->scene.extFieldLen) {
            pCmd->scene.extFieldLen = (pCmd->scene.extFieldLen > ZCL_MAX_SCENE_EXT_FIELD_SIZE) ? ZCL_MAX_SCENE_EXT_FIELD_SIZE
                                                                                               : pCmd->scene.extFieldLen;
            memcpy(pCmd->scene.extField, pData, pCmd->scene.extFieldLen);
        }
    }
}

_CODE_ZCL_ static void zcl_getSceneMembershipRspParse(uint8_t *pData, getSceneMemRsp_t *pCmd)
{
    pCmd->status = *pData++;
    pCmd->capacity = *pData++;
    pCmd->groupId = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    if (pCmd->status == ZCL_STA_SUCCESS) {
        pCmd->sceneCnt = *pData++;
        pCmd->sceneList = pData;
    }
}

_CODE_ZCL_ static status_t zcl_addScenePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;
    uint8_t cmdId = pInMsg->hdr.cmd;

    addScene_t addScene;
    ZB_SETSTRUCTCONTENT(addScene, 0);

    zcl_addSceneParse(endpoint, pInMsg->pData, pInMsg->dataLen, &addScene);

    if (cmdId == ZCL_CMD_SCENE_ENHANCED_ADD_SCENE) {
        addScene.scene.transTime100ms = addScene.scene.transTime % 10;
        addScene.scene.transTime /= 10;
    }

    /* check if group id is valid */
    if ((addScene.scene.groupId == 0) || aps_group_search(addScene.scene.groupId, endpoint)) {
        status = zcl_scene_addSceneEntry(endpoint, &addScene.scene);
    } else {
        status = ZCL_STA_INVALID_FIELD;
    }

    if (UNICAST_MSG(pApsdeInd)) {
        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_scene_sceneRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, cmdId, status, addScene.scene.groupId, addScene.scene.sceneId);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_viewScenePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;
    uint8_t cmdId = pInMsg->hdr.cmd;
    uint8_t *pData = pInMsg->pData;

    viewScene_t viewScene;
    viewScene.groupId = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    viewScene.sceneId = *pData++;

    zcl_sceneTable_t *pSceneEntry = zcl_scene_findEntry(endpoint, viewScene.groupId, viewScene.sceneId);
    if (!pSceneEntry) {
        if ((viewScene.groupId != 0) && (aps_group_search(viewScene.groupId, endpoint) == NULL)) {
            /* group not found */
            status = ZCL_STA_INVALID_FIELD;
        } else {
            status = ZCL_STA_NOT_FOUND;
        }
    } else {
        status = ZCL_STA_SUCCESS;
    }

    if (UNICAST_MSG(pApsdeInd)) {
        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_sceneEntry_t *pScene = (status == ZCL_STA_SUCCESS) ? &pSceneEntry->scene : NULL;

        zcl_scene_viewSceneRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, cmdId, status, viewScene.groupId, viewScene.sceneId, pScene);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_removeScenePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;
    uint8_t *pData = pInMsg->pData;

    removeScene_t removeScene;
    removeScene.groupId = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    removeScene.sceneId = *pData++;

    status = zcl_scene_removeSceneEntry(endpoint, removeScene.groupId, removeScene.sceneId);
    if (status == ZCL_STA_NOT_FOUND) {
        /* scene not found, check if group exist */
        if (aps_group_search(removeScene.groupId, endpoint) == NULL) {
            status = ZCL_STA_INVALID_FIELD;
        }
    }

    if (UNICAST_MSG(pApsdeInd)) {
        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_scene_sceneRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, ZCL_CMD_SCENE_REMOVE_SCENE_RSP, status, removeScene.groupId, removeScene.sceneId);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_removeAllScenePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    removeAllScene_t removeAllScene;
    removeAllScene.groupId = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);

    if ((removeAllScene.groupId == 0) || aps_group_search(removeAllScene.groupId, endpoint)) {
        zcl_scene_removeAllSceneEntry(endpoint, removeAllScene.groupId, TRUE);
    } else {
        status = ZCL_STA_INVALID_FIELD;
    }

    if (UNICAST_MSG(pApsdeInd)) {
        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_scene_sceneRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, ZCL_CMD_SCENE_REMOVE_ALL_SCENE_RSP, status, removeAllScene.groupId, 0);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_storeScenePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;
    uint8_t *pData = pInMsg->pData;

    storeScene_t storeScene;
    storeScene.groupId = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    storeScene.sceneId = *pData++;

    if ((storeScene.groupId == 0) || aps_group_search(storeScene.groupId, endpoint)) {
        zcl_sceneTable_t *pSceneEntry = zcl_scene_findEntry(endpoint, storeScene.groupId, storeScene.sceneId);
        if (!pSceneEntry) {
            pSceneEntry = zcl_scene_getFreeEntry();
            if (!pSceneEntry) {
                /* scene table full */
                status = ZCL_STA_INSUFFICIENT_SPACE;
            }
        }

        if (status == ZCL_STA_SUCCESS) {
            if (pInMsg->clusterAppCb) {
                /* Notify APP to fill scenes' information */
                pInMsg->clusterAppCb(&(pInMsg->addrInfo), ZCL_CMD_SCENE_STORE_SCENE, &pSceneEntry->scene);

                /* store scene */
                pSceneEntry->used = 1;
                pSceneEntry->endpoint = endpoint;
                pSceneEntry->scene.groupId = storeScene.groupId;
                pSceneEntry->scene.sceneId = storeScene.sceneId;

                /* update scene attributes */
                uint16_t attrLen = 0;
                uint8_t currentScene = 0;
                if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_SCENE, &attrLen, (uint8_t *)&currentScene) == ZCL_STA_SUCCESS) {
                    currentScene = storeScene.sceneId;
                    zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_SCENE, (uint8_t *)&currentScene);
                }

                uint16_t currentGroup = 0;
                if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_GROUP, &attrLen, (uint8_t *)&currentGroup) == ZCL_STA_SUCCESS) {
                    currentGroup = storeScene.groupId;
                    zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_GROUP, (uint8_t *)&currentGroup);
                }

                uint8_t sceneValid = 0;
                if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_VALID, &attrLen, (uint8_t *)&sceneValid) == ZCL_STA_SUCCESS) {
                    sceneValid = 1;
                    zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_VALID, (uint8_t *)&sceneValid);
                }

                /* scenes save */
                zcl_scenesSave(endpoint);
            } else {
                status = ZCL_STA_FAILURE;
            }
        }
    } else {
        status = ZCL_STA_INVALID_FIELD;
    }

    if (UNICAST_MSG(pApsdeInd)) {
        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_scene_sceneRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, ZCL_CMD_SCENE_STORE_SCENE_RSP, status, storeScene.groupId, storeScene.sceneId);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_recallScenePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;
    uint8_t *pData = pInMsg->pData;

    recallScene_t recallScene;
    ZB_SETSTRUCTCONTENT(recallScene, 0);

    recallScene.groupId = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    recallScene.sceneId = *pData++;
    recallScene.transTime = 0xFFFF;
    if (pInMsg->dataLen == 5) {
        recallScene.transTime = BUILD_U16(pData[0], pData[1]);
        pData += 2;
    }

    zcl_sceneTable_t *pSceneEntry = zcl_scene_findEntry(endpoint, recallScene.groupId, recallScene.sceneId);
    if (pSceneEntry) {
        if (pInMsg->clusterAppCb) {
            /* store transTime */
            uint16_t transTime = pSceneEntry->scene.transTime;
            uint16_t transTime100ms = pSceneEntry->scene.transTime100ms;
            if (recallScene.transTime != 0xFFFF) {
                pSceneEntry->scene.transTime = recallScene.transTime / 10;
                pSceneEntry->scene.transTime100ms = recallScene.transTime % 10;
            }

            pInMsg->clusterAppCb(&(pInMsg->addrInfo), ZCL_CMD_SCENE_RECALL_SCENE, &pSceneEntry->scene);

            /* restore transTime */
            pSceneEntry->scene.transTime = transTime;
            pSceneEntry->scene.transTime100ms = transTime100ms;

            /* update scene attributes */
            uint16_t attrLen = 0;
            uint8_t currentScene = 0;
            if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_SCENE, &attrLen, (uint8_t *)&currentScene) == ZCL_STA_SUCCESS) {
                currentScene = recallScene.sceneId;
                zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_SCENE, (uint8_t *)&currentScene);
            }

            uint16_t currentGroup = 0;
            if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_GROUP, &attrLen, (uint8_t *)&currentGroup) == ZCL_STA_SUCCESS) {
                currentGroup = recallScene.groupId;
                zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_CURRENT_GROUP, (uint8_t *)&currentGroup);
            }

            uint8_t sceneValid = 0;
            if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_VALID, &attrLen, (uint8_t *)&sceneValid) == ZCL_STA_SUCCESS) {
                sceneValid = 1;
                zcl_setAttrVal(endpoint, ZCL_CLUSTER_GEN_SCENES, ZCL_ATTRID_SCENE_SCENE_VALID, (uint8_t *)&sceneValid);
            }
        }
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_getSceneMembershipPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    getSceneMemship_t getSceneMembership;
    getSceneMembership.groupId = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);

    uint8_t sceneCnt = 0;
    uint8_t sceneList[ZCL_SCENE_TABLE_NUM] = {0};

    if ((getSceneMembership.groupId == 0) || aps_group_search(getSceneMembership.groupId, endpoint)) {
        sceneCnt = zcl_scene_findAllSceneByGroup(endpoint, getSceneMembership.groupId, sceneList);
    } else {
        status = ZCL_STA_INVALID_FIELD;
    }

    if (UNICAST_MSG(pApsdeInd)) {
        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        getSceneMemRsp_t getSceneMembershipRsp;
        getSceneMembershipRsp.status = status;
        getSceneMembershipRsp.capacity = ZCL_SCENE_TABLE_NUM - zcl_scene_getSceneUsedNum();
        getSceneMembershipRsp.groupId = getSceneMembership.groupId;
        getSceneMembershipRsp.sceneCnt = sceneCnt;
        getSceneMembershipRsp.sceneList = sceneList;

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_scene_getSceneMembershipRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, &getSceneMembershipRsp);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_copyScenePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;
    uint8_t *pData = pInMsg->pData;

    copyScene_t copyScene;
    copyScene.mode = *pData++;
    copyScene.groupIdFrom = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    copyScene.sceneIdFrom = *pData++;
    copyScene.groupIdTo = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    copyScene.sceneIdTo = *pData++;

    if (aps_group_search(copyScene.groupIdFrom, endpoint) && aps_group_search(copyScene.groupIdTo, endpoint)) {
        status = zcl_scene_copySceneEntry(endpoint, &copyScene);
    } else {
        /* group not found */
        status = ZCL_STA_INVALID_FIELD;
    }

    if (UNICAST_MSG(pApsdeInd)) {
        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_scene_sceneRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, ZCL_CMD_SCENE_COPY_SCENE_RSP, status, copyScene.groupIdFrom, copyScene.sceneIdFrom);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_scene_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_SCENE_ADD_SCENE:
    case ZCL_CMD_SCENE_ENHANCED_ADD_SCENE:
        status = zcl_addScenePrc(pInMsg);
        break;
    case ZCL_CMD_SCENE_VIEW_SCENE:
    case ZCL_CMD_SCENE_ENHANCED_VIEW_SCENE:
        status = zcl_viewScenePrc(pInMsg);
        break;
    case ZCL_CMD_SCENE_REMOVE_SCENE:
        status = zcl_removeScenePrc(pInMsg);
        break;
    case ZCL_CMD_SCENE_REMOVE_ALL_SCENE:
        status = zcl_removeAllScenePrc(pInMsg);
        break;
    case ZCL_CMD_SCENE_STORE_SCENE:
        status = zcl_storeScenePrc(pInMsg);
        break;
    case ZCL_CMD_SCENE_RECALL_SCENE:
        status = zcl_recallScenePrc(pInMsg);
        break;
    case ZCL_CMD_SCENE_GET_SCENE_MEMSHIP:
        status = zcl_getSceneMembershipPrc(pInMsg);
        break;
    case ZCL_CMD_SCENE_COPY_SCENE:
        status = zcl_copyScenePrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_scene_serverCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    zcl_scene_cmdPayload_t cmdPayload;
    memset((uint8_t *)&cmdPayload, 0, sizeof(zcl_scene_cmdPayload_t));

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_SCENE_ADD_SCENE_RSP:
    case ZCL_CMD_SCENE_ENHANCED_ADD_SCENE_RSP:
        //status = zcl_addSceneRspPrc(pInMsg);
        cmdPayload.addSceneRsp.status = *pData++;
        cmdPayload.addSceneRsp.groupId = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmdPayload.addSceneRsp.sceneId = *pData++;
        break;
    case ZCL_CMD_SCENE_VIEW_SCENE_RSP:
    case ZCL_CMD_SCENE_ENHANCED_VIEW_SCENE_RSP:
        //status = zcl_viewSceneRspPrc(pInMsg);
        zcl_viewSceneRspParse(pInMsg->pData, pInMsg->dataLen, &cmdPayload.viewSceneRsp);
        break;
    case ZCL_CMD_SCENE_REMOVE_SCENE_RSP:
        //status = zcl_removeSceneRspPrc(pInMsg);
        cmdPayload.removeSceneRsp.status = *pData++;
        cmdPayload.removeSceneRsp.groupId = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmdPayload.removeSceneRsp.sceneId = *pData++;
        break;
    case ZCL_CMD_SCENE_REMOVE_ALL_SCENE_RSP:
        //status = zcl_removeAllSceneRspPrc(pInMsg);
        cmdPayload.removeAllSceneRsp.status = *pData++;
        cmdPayload.removeAllSceneRsp.groupId = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        break;
    case ZCL_CMD_SCENE_STORE_SCENE_RSP:
        //status = zcl_storeSceneRspPrc(pInMsg);
        cmdPayload.storeSceneRsp.status = *pData++;
        cmdPayload.storeSceneRsp.groupId = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmdPayload.storeSceneRsp.sceneId = *pData++;
        break;
    case ZCL_CMD_SCENE_GET_SCENE_MEMSHIP_RSP:
        //status = zcl_getSceneMembershipRspPrc(pInMsg);
        zcl_getSceneMembershipRspParse(pInMsg->pData, &cmdPayload.getSceneMembershipRsp);
        break;
    case ZCL_CMD_SCENE_COPY_SCENE_RSP:
        //status = zcl_copySceneRspPrc(pInMsg);
        cmdPayload.copySceneRsp.status = *pData++;
        cmdPayload.copySceneRsp.groupIdFrom = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        cmdPayload.copySceneRsp.sceneIdFrom = *pData++;
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    if (status == ZCL_STA_SUCCESS) {
        if (pInMsg->clusterAppCb) {
            pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &cmdPayload);
        }
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_scene_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_scene_clientCmdHandler(pInMsg);
    } else {
        return zcl_scene_serverCmdHandler(pInMsg);
    }
}

#endif  /* ZCL_SCENE */
