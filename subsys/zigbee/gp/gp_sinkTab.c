/********************************************************************************************************
 * @file    gp_sinkTab.c
 *
 * @brief   This is the source file for gp_sinkTab
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
#include "zb_common.h"
#include "../zcl/zcl_include.h"
#include "gp_base.h"
#include "gp.h"
#include "gp_sink.h"

#if (GP_SUPPORT_ENABLE && GP_BASIC_COMBO)

gp_sinkTab_t g_gpSinkTab;


void gp_sinkTabSave2Flash(void *arg)
{
#if NV_ENABLE
    nv_flashWriteNew(1, NV_MODULE_ZCL, NV_ITEM_ZCL_GP_SINK_TABLE, sizeof(gp_sinkTab_t), (uint8_t *)&g_gpSinkTab);
#endif
}

nv_sts_t gp_sinkTabRestoreFromFlash(void)
{
    uint8_t ret = NV_SUCC;

#if NV_ENABLE
    ret = nv_flashReadNew(1, NV_MODULE_ZCL,  NV_ITEM_ZCL_GP_SINK_TABLE, sizeof(gp_sinkTab_t), (uint8_t *)&g_gpSinkTab);
#endif

    return ret;
}

uint8_t gp_getSinkTabEntryTotalNum(void)
{
    return g_gpSinkTab.gpSinkTabNum;
}

gpSinkTabEntry_t *gp_sinkTabEntryFreeGet(void)
{
    gpSinkTabEntry_t *pEntry = NULL;

    if (g_gpSinkTab.gpSinkTabNum >= zclGpAttr_gpsMaxSinkTabEntries) {
        return NULL;
    }

    for (uint8_t i = 0; i < zclGpAttr_gpsMaxSinkTabEntries; i++) {
        pEntry = &g_gpSinkTab.gpSinkTab[i];
        if (!pEntry->used) {
            pEntry->gpdSecFrameCnt = 0xffffffff;
            pEntry->groupcastRadius = 0xff;
            pEntry->gpdAssignedAlias = 0xffff;
            memset((uint8_t *)pEntry->groupList, 0xff, sizeof(gpSinkGroupListItem_t) * SINK_GROUP_LIST_NUM);

            return pEntry;
        }
    }

    return NULL;
}

void gp_sinkRemoveEntries(uint8_t appId, gpdId_t gpdId, uint8_t endpoint)
{
    gpSinkTabEntry_t *pEntry = NULL;

    //printf("sinkRemove: appId = %x, ep = %x\n", appId, endpoint);

    if (g_gpSinkTab.gpSinkTabNum == 0) {
        //printf("empty\n");
        return;
    }

    uint8_t findEntry = 0;
    uint8_t findEntryTotal = 0;
    for (uint8_t i = 0; i < zclGpAttr_gpsMaxSinkTabEntries; i++) {
        pEntry = &g_gpSinkTab.gpSinkTab[i];
        if ((pEntry->used) && (pEntry->options.bits.appId == appId)) {
            if ((appId == GP_APP_ID_SRC_ID) && !memcmp((uint8_t *)&gpdId.srcId, (uint8_t *)&pEntry->gpdId.srcId, SRC_ID_LEN)) {
                findEntry++;
            } else if ((appId == GP_APP_ID_GPD) && !memcmp((uint8_t *)&gpdId.gpdIeeeAddr, (uint8_t *)&pEntry->gpdId.gpdIeeeAddr, EXT_ADDR_LEN)) {
                if ((endpoint == pEntry->endpoint) || (pEntry->endpoint == 0) || (pEntry->endpoint == 0xff) || (endpoint == 0xff)) {
                    findEntry++;
                }
            }
            if (findEntry) {
                findEntry = 0;
                findEntryTotal++;
                aps_delete_group_req_t req;
                ZB_SETSTRUCTCONTENT(req, 0);

                for (uint8_t i = 0; i < pEntry->sinkGroupListCnt; i++) {
                    req.group_addr = pEntry->groupList[i].groupId;
                    req.ep = GREEN_POWER_ENDPOINT;
                    aps_me_group_delete_req(&req);
                }
                if (pEntry->options.bits.communicationMode == GPS_COMM_MODE_GROUP_DGROUPID) {
                    req.ep = GREEN_POWER_ENDPOINT;
                    req.group_addr = gpAliasSrcAddrDerived(pEntry->options.bits.appId, pEntry->gpdId);

                    //printf("rmGrp: ep = %x, addrGrp = %x\n", req.ep, req.group_addr);

                    aps_me_group_delete_req(&req);
                }
                memset((uint8_t *)pEntry, 0, sizeof(gpSinkTabEntry_t));
                g_gpSinkTab.gpSinkTabNum--;
            }
        }
    }

    if (findEntryTotal) {
        gpSinkTabUpdate();
    }
}

gpSinkTabEntry_t *gp_getSinkTabByGpdId(uint8_t appId, gpdId_t gpdId)
{
    gpSinkTabEntry_t *pEntry = NULL;

    if (g_gpSinkTab.gpSinkTabNum == 0) {
        return NULL;
    }

    for (uint8_t i = 0; i < zclGpAttr_gpsMaxSinkTabEntries; i++) {
        pEntry = &g_gpSinkTab.gpSinkTab[i];
        if (pEntry->used) {
            if (pEntry->options.bits.appId == appId) {
                if ((appId == GP_APP_ID_SRC_ID) && !memcmp((uint8_t *)&gpdId.srcId, (uint8_t *)&pEntry->gpdId.srcId, SRC_ID_LEN)) {
                    return pEntry;
                } else if ((appId == GP_APP_ID_GPD) && !memcmp((uint8_t *)&gpdId.gpdIeeeAddr, (uint8_t *)&pEntry->gpdId.gpdIeeeAddr, EXT_ADDR_LEN)) {
                    return pEntry;
                }
            }
        }
    }

    return NULL;
}

void gp_sinkGroupListAdd(gpSinkTabEntry_t *pEntry, uint8_t endpoint)
{
    if (pEntry->sinkGroupListCnt >= SINK_GROUP_LIST_NUM) {
        return;
    }

    for (uint8_t i = 0; i < APS_GROUP_TABLE_SIZE; i++) {
        aps_group_tbl_ent_t *pGroupEntry = &aps_group_tbl[i];

        if (pGroupEntry->group_addr != 0xffff) {
            for (uint8_t epNum = 0; epNum < pGroupEntry->n_endpoints; epNum++) {
                if (pGroupEntry->endpoints[epNum] == endpoint) {
                    for (uint8_t j = 0; j < pEntry->sinkGroupListCnt; j++) {
                        if (pEntry->groupList[j].groupId == pGroupEntry->group_addr) {
                            return;
                        }
                    }

                    pEntry->groupList[pEntry->sinkGroupListCnt].groupId = pGroupEntry->group_addr;
                    pEntry->groupList[pEntry->sinkGroupListCnt].alias = 0xFFFF;//TODO: ?
                    pEntry->sinkGroupListCnt++;

                    if (pEntry->sinkGroupListCnt >= SINK_GROUP_LIST_NUM) {
                        return;
                    } else {
                        break;
                    }
                }
            }
        }
    }
}

uint8_t gp_getSinkTabEntryLen(gpSinkTabEntry_t *pEntry)
{
    uint8_t entryLen = 2;//options

    if (pEntry->options.bits.appId == GP_APP_ID_SRC_ID) {
        entryLen += 4;
    } else if (pEntry->options.bits.appId == GP_APP_ID_GPD) {
        entryLen += EXT_ADDR_LEN;
        entryLen += 1;//endpoint
    }

    entryLen += sizeof(uint8_t);//DeviceID

    if (pEntry->options.bits.communicationMode == GPS_COMM_MODE_GROUP_PRE_COMMISSIONED_GROUPID) {
        entryLen += 1;
        if (pEntry->sinkGroupListCnt) {
            entryLen += pEntry->sinkGroupListCnt * sizeof(gpSinkGroupListItem_t);
        }
    }

    if (pEntry->options.bits.assignedAlias) {
        entryLen += sizeof(uint16_t);
    }

    entryLen += sizeof(uint8_t);//groupcast radius

    if (pEntry->options.bits.securityUse) {
        entryLen += sizeof(uint8_t);
    }

    if ((pEntry->options.bits.securityUse) || (pEntry->options.bits.seqNumCapabilities)) {
        entryLen += sizeof(uint32_t);
    }

    if (!ZB_SEC_KEY_IS_NULL(pEntry->gpdKey)) {
        entryLen += SEC_KEY_LEN;
    }

    return entryLen;
}

uint8_t gp_buildSinkTabEntryFormat(gpSinkTabEntry_t *pEntry, uint8_t *pBuf)
{
    uint8_t *ptr = pBuf;

    //Options
    *ptr++ = LO_UINT16(pEntry->options.opts);
    *ptr++ = HI_UINT16(pEntry->options.opts);

    //GPD ID and Endpoint
    if (pEntry->options.bits.appId == GP_APP_ID_SRC_ID) {
        *ptr++ = U32_BYTE0(pEntry->gpdId.srcId);
        *ptr++ = U32_BYTE1(pEntry->gpdId.srcId);
        *ptr++ = U32_BYTE2(pEntry->gpdId.srcId);
        *ptr++ = U32_BYTE3(pEntry->gpdId.srcId);
    } else if (pEntry->options.bits.appId == GP_APP_ID_GPD) {
        ZB_64BIT_ADDR_COPY(ptr, pEntry->gpdId.gpdIeeeAddr);
        ptr += EXT_ADDR_LEN;
        *ptr++ = pEntry->endpoint;
    }

    *ptr++ = pEntry->deviceId;

    if (pEntry->options.bits.communicationMode == GPS_COMM_MODE_GROUP_PRE_COMMISSIONED_GROUPID) {
        *ptr++ = pEntry->sinkGroupListCnt;
        for (uint8_t i = 0; i < pEntry->sinkGroupListCnt; i++) {
            *ptr++ = LO_UINT16(pEntry->groupList[i].groupId);
            *ptr++ = HI_UINT16(pEntry->groupList[i].groupId);
            *ptr++ = LO_UINT16(pEntry->groupList[i].alias);
            *ptr++ = HI_UINT16(pEntry->groupList[i].alias);
        }
    }

    //GPD Assigned Alias
    if (pEntry->options.bits.assignedAlias) {
        *ptr++ = LO_UINT16(pEntry->gpdAssignedAlias);
        *ptr++ = HI_UINT16(pEntry->gpdAssignedAlias);
    }

    *ptr++ = pEntry->groupcastRadius;

    //Security Options
    if (pEntry->options.bits.securityUse) {
        *ptr++ = pEntry->secOptions.opts;
    }

    //GPD security frame counter
    if ((pEntry->options.bits.securityUse) || (pEntry->options.bits.seqNumCapabilities)) {
        *ptr++ = U32_BYTE0(pEntry->gpdSecFrameCnt);
        *ptr++ = U32_BYTE1(pEntry->gpdSecFrameCnt);
        *ptr++ = U32_BYTE2(pEntry->gpdSecFrameCnt);
        *ptr++ = U32_BYTE3(pEntry->gpdSecFrameCnt);
    }

    //GPD key
    if (!ZB_SEC_KEY_IS_NULL(pEntry->gpdKey)) {
        memcpy(ptr, pEntry->gpdKey, SEC_KEY_LEN);
        ptr += SEC_KEY_LEN;
    }

    return (ptr - pBuf);
}

static void zclGpSinkTabAttrUpdate(void)
{
    uint8_t *pBuf = zclGpAttr_sinkTabEntry;

    if (g_gpSinkTab.gpSinkTabNum == 0) {
        //len = 0
        *pBuf++ = 0;
        *pBuf++ = 0;
    } else {
        uint8_t entryLen = 0;
        uint8_t maxEntryLen = 0;
        uint8_t entryCnt = 0;

        for (uint8_t i = 0; i < zclGpAttr_gpsMaxSinkTabEntries; i++) {
            maxEntryLen = entryLen;

            if (g_gpSinkTab.gpSinkTab[i].used) {
                entryLen += gp_getSinkTabEntryLen(&g_gpSinkTab.gpSinkTab[i]);

                if (entryLen >= ZCL_GP_MAX_SINK_TABLE_ATTR_LEN) {
                    entryLen = maxEntryLen;
                    break;
                } else {
                    entryCnt++;
                }
            }
        }
        *pBuf++ = LO_UINT16(entryLen);
        *pBuf++ = 0; //HI_UINT16(entryLen) always is 0

        for (uint8_t i = 0; i < entryCnt; i++) {
            if (g_gpSinkTab.gpSinkTab[i].used) {
                pBuf += gp_buildSinkTabEntryFormat(&g_gpSinkTab.gpSinkTab[i], pBuf);
            }
        }
    }
}

void gpSinkTabUpdate(void)
{
    zclGpSinkTabAttrUpdate();

    //write to NV
    ZB_SCHEDULE_TASK(gp_sinkTabSave2Flash, NULL);
}

void gpSinkTabInit(void)
{
    if (gp_sinkTabRestoreFromFlash() == NV_SUCC) {
        zclGpSinkTabAttrUpdate();
    } else {
        memset((uint8_t *)&g_gpSinkTab, 0, sizeof(gp_sinkTab_t));
    }
}

#endif	/* GP_SUPPORT_ENABLE */
