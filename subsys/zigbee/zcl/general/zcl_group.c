/********************************************************************************************************
 * @file    zcl_group.c
 *
 * @brief   This is the source file for zcl_group
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


#ifdef ZCL_GROUP
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
static status_t zcl_group_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_group_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    return zcl_registerCluster(endpoint, ZCL_CLUSTER_GEN_GROUPS, manuCode, attrNum, attrTbl, zcl_group_cmdHandler, cb);
}


_CODE_ZCL_ status_t zcl_group_add(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint16_t groupId, uint8_t *groupName)
{
    uint8_t buf[20];

    uint8_t *pBuf = buf;

    *pBuf++ = LO_UINT16(groupId);
    *pBuf++ = HI_UINT16(groupId);
    if (!groupName) {
        *pBuf++ = 0;
    } else {
        uint8_t len = groupName[0];
        len = (len > 15) ? 15 : len;
        *pBuf++ = len;
        memcpy(pBuf, &groupName[1], len);
        pBuf += len;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_ADD_GROUP, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_group_view(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint16_t groupId)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_VIEW_GROUP, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, (uint8_t*)&groupId);
}

_CODE_ZCL_ status_t zcl_group_getMembership(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t groupCnt, uint16_t *groupList)
{
    uint8_t buf[2 * APS_GROUP_TABLE_NUM + 1];

    if (groupCnt > APS_GROUP_TABLE_NUM) {
        return ZCL_STA_INVALID_VALUE;
    }

    buf[0] = groupCnt;
    memcpy(&buf[1], groupList, groupCnt*2);

    return  zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_GET_MEMBERSHIP, TRUE,
                        ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, groupCnt * 2 + 1, buf);
}

_CODE_ZCL_ status_t zcl_group_remove(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint16_t groupId)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_REMOVE_GROUP, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, (uint8_t*)&groupId);
}

_CODE_ZCL_ status_t zcl_group_removeAll(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_REMOVE_ALL_GROUP, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_group_addIfIdentify(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint16_t groupId, uint8_t *groupName)
{
    uint8_t buf[20];

    uint8_t *pBuf = buf;

    *pBuf++ = LO_UINT16(groupId);
    *pBuf++ = HI_UINT16(groupId);
    if (!groupName) {
        *pBuf++ = 0;
    } else {
        uint8_t len = groupName[0];
        len = (len > 15) ? 15 : len;
        *pBuf++ = len;
        memcpy(pBuf, &groupName[1], len);
        pBuf += len;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_ADD_GROUP_IF_IDF, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}



_CODE_ZCL_ status_t zcl_group_addGroupRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint16_t groupId, uint8_t stauts)
{
    uint8_t buf[3];

    buf[0] = stauts;
    buf[1] = LO_UINT16(groupId);
    buf[2] = HI_UINT16(groupId);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_ADD_GROUP_RSP, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 3, buf);
}

_CODE_ZCL_ status_t zcl_group_viewGroupRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_viewGroupRsp_t *pViewGroupRsp)
{
    uint8_t buf[20];

    uint8_t *pBuf = buf;

    *pBuf++ = pViewGroupRsp->status;
    *pBuf++ = LO_UINT16(pViewGroupRsp->groupId);
    *pBuf++ = HI_UINT16(pViewGroupRsp->groupId);
    if (pViewGroupRsp->pGroupName == NULL) {
        *pBuf++ = 0;//group name length set to zero
    } else {
        uint8_t len = pViewGroupRsp->pGroupName[0];
        len = (len > 15) ? 15 : len;
        *pBuf++ = len;
        memcpy(pBuf, &pViewGroupRsp->pGroupName[1], len);
        pBuf += len;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_VIEW_GROUP_RSP, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_group_getGroupMembershipRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t capacity, uint8_t groupCnt, uint16_t *groupList)
{
    uint8_t *buf = (uint8_t *)groupList;
    buf[0] = capacity;
    buf[1] = groupCnt;
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_GET_MEMBERSHIP_RSP, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, groupCnt * 2 + 2, buf);
}

_CODE_ZCL_ status_t zcl_group_removeGroupRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint16_t groupId, uint8_t stauts)
{
    uint8_t buf[3];

    buf[0] = stauts;
    buf[1] = LO_UINT16(groupId);
    buf[2] = HI_UINT16(groupId);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_GEN_GROUPS, ZCL_CMD_GROUP_REMOVE_GROUP_RSP, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 3, buf);
}

_CODE_ZCL_ static status_t zcl_addGroupPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t *)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    aps_add_group_req_t addGroup;

    addGroup.group_addr = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);
    addGroup.ep = pApsdeInd->indInfo.dst_ep;

    status = aps_me_group_add_req(&addGroup);
    if (status == APS_STATUS_DUPLICATE_ENTRY) {
        status = ZCL_STA_DUPLICATE_EXISTS;
    } else if (status == APS_STATUS_TABLE_FULL) {
        status = ZCL_STA_INSUFFICIENT_SPACE;
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

        zcl_group_addGroupRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, addGroup.group_addr, status);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_viewGroupPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t *)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    if (UNICAST_MSG(pApsdeInd)) {
        zcl_viewGroupRsp_t viewGroupRsp;

        viewGroupRsp.groupId = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);

        aps_group_tbl_ent_t *pEntry = aps_group_search(viewGroupRsp.groupId, endpoint);
        if (!pEntry) {
            status = ZCL_STA_NOT_FOUND;
        }

        viewGroupRsp.pGroupName = NULL;//not support
        viewGroupRsp.status = status;

        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_group_viewGroupRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, &viewGroupRsp);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_getGroupMembershipPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t *)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    if (UNICAST_MSG(pApsdeInd)) {
        uint8_t *pBuf = pInMsg->pData;
        uint8_t groupCnt = *pBuf++;
        uint16_t groupListBuf[1+APS_GROUP_TABLE_NUM];
        uint16_t *findGroupList = groupListBuf+1;
        uint8_t findCnt = 0;

        if (groupCnt == 0) {
            aps_group_list_get(&findCnt, findGroupList);
        } else {
            while (groupCnt--) {
                uint16_t groupId = BUILD_U16(pBuf[0], pBuf[1]);

                aps_group_tbl_ent_t *pEntry = aps_group_search(groupId, endpoint);
                if (pEntry) {
                    findGroupList[findCnt++] = groupId;
                    if (findCnt >= APS_GROUP_TABLE_SIZE) {
                        break;
                    }
                }
                pBuf += 2;
            }
        }

        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_group_getGroupMembershipRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum,
                                       (APS_GROUP_TABLE_SIZE - aps_group_entry_num_get()), findCnt, groupListBuf);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_removeGroupPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t *)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    aps_delete_group_req_t removeGroup;
    removeGroup.ep = endpoint;
    removeGroup.group_addr = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);

#ifdef ZCL_SCENE
    zcl_scene_removeAllSceneEntry(endpoint, removeGroup.group_addr, TRUE);
#endif

    status = aps_me_group_delete_req(&removeGroup);
    if (status == APS_STATUS_INVALID_GROUP) {
        status = ZCL_STA_NOT_FOUND;
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

        zcl_group_removeGroupRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, removeGroup.group_addr, status);

        status = ZCL_STA_CMD_HAS_RESP;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_removeAllGroupPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t *)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

#ifdef ZCL_SCENE
    uint8_t groupCnt = 0;
    uint16_t groupList[APS_GROUP_TABLE_NUM] = {0};

    aps_group_list_get(&groupCnt, groupList);

    for (uint8_t i = 0; i < groupCnt; i++) {
        zcl_scene_removeAllSceneEntry(endpoint, groupList[i], ((i >= groupCnt) ? TRUE : FALSE));
    }
#endif

    aps_me_group_delete_all_req(endpoint);

    return status;
}

_CODE_ZCL_ static status_t zcl_addGroupIfIdentifyPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t *)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    uint16_t identifyTime = 0;
    uint16_t len = 0;

    if (zcl_getAttrVal(endpoint, ZCL_CLUSTER_GEN_IDENTIFY, ZCL_ATTRID_IDENTIFY_TIME, &len, (uint8_t *)&identifyTime) == ZCL_STA_SUCCESS) {
        if (identifyTime) {
            aps_add_group_req_t addGroup;

            addGroup.group_addr = BUILD_U16(pInMsg->pData[0], pInMsg->pData[1]);
            addGroup.ep = pApsdeInd->indInfo.dst_ep;

            status = aps_me_group_add_req(&addGroup);
            if (status == APS_STATUS_DUPLICATE_ENTRY) {
                status = ZCL_STA_DUPLICATE_EXISTS;
            } else if (status == APS_STATUS_TABLE_FULL) {
                status = ZCL_STA_INSUFFICIENT_SPACE;
            } else {
                status = ZCL_STA_SUCCESS;
            }
        }
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_addGroupRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_addGroupRsp_t addGroupRsp;
        addGroupRsp.status = *pData++;
        addGroupRsp.groupId = BUILD_U16(pData[0], pData[1]);

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &addGroupRsp);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_viewGroupRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_viewGroupRsp_t viewGroupRsp;
        viewGroupRsp.status = *pData++;
        viewGroupRsp.groupId = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        if (viewGroupRsp.status == ZCL_STA_SUCCESS) {
            viewGroupRsp.pGroupName = pData;
        } else {
            viewGroupRsp.pGroupName = NULL;
        }

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &viewGroupRsp);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_removeGroupRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_removeGroupRsp_t removeGroupRsp;
        removeGroupRsp.status = *pData++;
        removeGroupRsp.groupId = BUILD_U16(pData[0], pData[1]);

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &removeGroupRsp);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_getGroupMembershipRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb ){
        zcl_getGroupMembershipRsp_t getGroupMembershipRsp;
        getGroupMembershipRsp.capacity = *pData++;
        getGroupMembershipRsp.groupCnt = *pData++;
        getGroupMembershipRsp.pGroupList = pData;

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &getGroupMembershipRsp);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_group_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_GROUP_ADD_GROUP:
        status = zcl_addGroupPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_VIEW_GROUP:
        status = zcl_viewGroupPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_GET_MEMBERSHIP:
        status = zcl_getGroupMembershipPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_REMOVE_GROUP:
        status = zcl_removeGroupPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_REMOVE_ALL_GROUP:
        status = zcl_removeAllGroupPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_ADD_GROUP_IF_IDF:
        status = zcl_addGroupIfIdentifyPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ uint8_t zcl_group_serverCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_GROUP_ADD_GROUP_RSP:
        status = zcl_addGroupRspPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_VIEW_GROUP_RSP:
        status = zcl_viewGroupRspPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_REMOVE_GROUP_RSP:
        status = zcl_removeGroupRspPrc(pInMsg);
        break;
    case ZCL_CMD_GROUP_GET_MEMBERSHIP_RSP:
        status = zcl_getGroupMembershipRspPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_group_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_group_clientCmdHandler(pInMsg);
    } else {
        return zcl_group_serverCmdHandler(pInMsg);
    }
}

#endif  /* ZCL_GROUP */
