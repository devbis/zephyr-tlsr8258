/********************************************************************************************************
 * @file    zcl_ota.h
 *
 * @brief   This is the header file for zcl_ota
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
#ifndef ZCL_OTA_H
#define ZCL_OTA_H


/**
 *  @brief	zcl_ota_cmdId OTA Command Ids
 */
#define ZCL_CMD_OTA_IMAGE_NOTIFY                        0x00
#define ZCL_CMD_OTA_QUERY_NEXT_IMAGE_REQ                0x01
#define ZCL_CMD_OTA_QUERY_NEXT_IMAGE_RSP                0x02
#define ZCL_CMD_OTA_IMAGE_BLOCK_REQ                     0x03
#define ZCL_CMD_OTA_IMAGE_PAGE_REQ                      0x04
#define ZCL_CMD_OTA_IMAGE_BLOCK_RSP                     0x05
#define ZCL_CMD_OTA_UPGRADE_END_REQ                     0x06
#define ZCL_CMD_OTA_UPGRADE_END_RSP                     0x07
#define ZCL_CMD_OTA_QUERY_DEVICE_SPECIFIC_FILE_REQ      0x08
#define ZCL_CMD_OTA_QUERY_DEVICE_SPECIFIC_FILE_RSP      0x09

/**
 *  @brief	attributes of OTA upgrade cluster
 */
#define ZCL_ATTRID_OTA_UPGRADE_SERVER_ID                0x0000
#define ZCL_ATTRID_OTA_FILE_OFFSET                      0x0001
#define ZCL_ATTRID_OTA_CURRENT_FILE_VER                 0x0002
#define ZCL_ATTRID_OTA_CURRENT_ZIGBEE_STACK_VER         0x0003
#define ZCL_ATTRID_OTA_DOWNLOAD_FILE_VER                0x0004
#define ZCL_ATTRID_OTA_DOWNLOAD_ZIGBEE_STACK_VER        0x0005
#define ZCL_ATTRID_OTA_IMAGE_UPGRADE_STATUS             0x0006
#define ZCL_ATTRID_OTA_MANUFACTURER_ID                  0x0007
#define ZCL_ATTRID_OTA_IMAGE_TYPE_ID                    0x0008
#define ZCL_ATTRID_OTA_MINIMUM_BLOCK_PERIOD             0x0009
#define ZCL_ATTRID_OTA_IMAGE_STAMP                      0x000a

/**
 *  @brief	image upgrade status attribute values
 */
#define IMAGE_UPGRADE_STATUS_NORMAL                     0x00
#define IMAGE_UPGRADE_STATUS_DOWNLOAD_IN_PROGRESS       0x01
#define IMAGE_UPGRADE_STATUS_DOWNLOAD_COMPLETE          0x02
#define IMAGE_UPGRADE_STATUS_WAITING_TO_UPGRADE         0x03
#define IMAGE_UPGRADE_STATUS_COUNT_DOWN                 0x04
#define IMAGE_UPGRADE_STATUS_WAIT_FOR_MORE              0x05


//query next image request field control bitmask
#define IMAGE_FC_BITMASK_HARDWARE_VERSION_PRESENT       0x01

//image block request field control bitmask
#define BLOCK_FC_BITMASK_GENERIC                        0x00
#define BLOCK_FC_BITMASK_NODE_IEEE_PRESENT              0x01
#define BLOCK_FC_BITMASK_MIN_PERIOD_PRESENT             0x02

//image page request field control bitmask
#define PAGE_FC_BITMASK_NODE_IEEE_PRESENT               0x01

//image notify command payload type
#define IMAGE_NOTIFY_QUERY_JITTER                       0x00
#define IMAGE_NOTIFY_QUERT_JITTER_MFG                   0x01
#define IMAGE_NOTIFY_QUERY_JITTER_MFG_TYPE              0x02
#define IMAGE_NOTIFY_QUERY_JITTER_MFG_TYPE_VER          0x03


typedef struct {
    uint8_t payloadType;
    uint8_t queryJitter;
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t newFileVer;
} ota_imageNotify_t;

typedef struct {
    uint8_t fc;//BIT(0) hardware version present
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t curFileVer;
    uint16_t hdrwareVer;
} ota_queryNextImageReq_t;

typedef struct {
    uint8_t st;
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
    uint32_t imageSize;
} ota_queryNextImageRsp_t;

typedef struct {
    uint8_t fc;//b0 req node's IEEE address present; b1 block request delay present
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
    uint32_t fileOffset;
    uint8_t maxDataSize;
    addrExt_t reqNodeAddr;
    uint16_t blockReqDelay;
} ota_imageBlockReq_t;

typedef struct {
    uint8_t fc;//b0 req node's IEEE address present; b1 block request delay present
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
    uint32_t fileOffset;
    uint8_t maxDataSize;
    uint16_t pageSize;
    uint16_t rspSpacing;
    addrExt_t reqNodeAddr;
} ota_imagePageReq_t;

typedef struct {
    uint8_t st;
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
} ota_upgradeEndReq_t;

typedef struct {
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
    uint32_t currentTime;
    uint32_t upgradeTime;
} ota_upgradeEndRsp_t;

typedef struct {
    addrExt_t reqNodeAddr;
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
    uint16_t zbStackVer;
} ota_querySpecFileReq_t;

typedef struct {
    uint8_t st;
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
    uint32_t imageSize;
} ota_querySpecFileRsp_t;

typedef struct {
    uint16_t manuCode;
    uint16_t imageType;
    uint32_t fileVer;
    uint32_t fileOffset;
    uint8_t dataSize;
    uint8_t *data;
} ota_imageBlockRspSuccess_t;

typedef struct {
    uint32_t currTime;
    uint32_t reqTime;
    uint16_t blockReqDelay;
} ota_imageBlockRspWait_t;

typedef union {
    ota_imageBlockRspSuccess_t success;
    ota_imageBlockRspWait_t wait;
} imageBlockRsp_t;

typedef struct {
    uint8_t st;
    imageBlockRsp_t rsp;
} ota_imageBlockRsp_t;

#if 0
/**
 *  @brief  Definition for ota command callback function type
 */
typedef status_t (*zcl_ota_imageNotifyCb_t)(apsdeDataInd_t *pInd, ota_imageNotify_t *pImageNotify);
typedef status_t (*zcl_ota_queryNextImageReqCb_t)(apsdeDataInd_t *pInd, ota_queryNextImageReq_t *pQueryNextImageReq);
typedef status_t (*zcl_ota_queryNextImageRspCb_t)(apsdeDataInd_t *pInd, ota_queryNextImageRsp_t *pQueryNextImageRsp);
typedef status_t (*zcl_ota_imageBlockReqCb_t)(apsdeDataInd_t *pInd, ota_imageBlockReq_t *pImageBlockReq);
typedef status_t (*zcl_ota_imagePageReqCb_t)(apsdeDataInd_t *pInd, ota_imagePageReq_t *pImagePageReq);
typedef status_t (*zcl_ota_imageBlockRspCb_t)(apsdeDataInd_t *pInd, ota_imageBlockRsp_t *pImageBlockRsp);
typedef status_t (*zcl_ota_upgradeEndReqCb_t)(apsdeDataInd_t *pInd, ota_upgradeEndReq_t *pUpgradeEndReq);
typedef status_t (*zcl_ota_upgradeEndRspCb_t)(apsdeDataInd_t *pInd, ota_upgradeEndRsp_t *pUpgradeEndRsp);
typedef status_t (*zcl_ota_queryDevSpecFileReqCb_t)(apsdeDataInd_t *pInd, ota_querySpecFileReq_t *pQueryDevSpecFileReq);
typedef status_t (*zcl_ota_queryDevSpecFileRspCb_t)(apsdeDataInd_t *pInd, ota_querySpecFileRsp_t *pQueryDevSpecFileRsp);


/**
 *  @brief  Structure definition for commands callback functions in OTA cluster
 */
typedef struct {
    zcl_ota_imageNotifyCb_t imageNotifyCbFunc;
    zcl_ota_queryNextImageReqCb_t queryNextImageReqCbFunc;
    zcl_ota_queryNextImageRspCb_t queryNextImageRspCbFunc;
    zcl_ota_imageBlockReqCb_t imageBlockReqCbFunc;
    zcl_ota_imagePageReqCb_t imagePageReqCbFunc;
    zcl_ota_imageBlockRspCb_t imageBlockRspCbFunc;
    zcl_ota_upgradeEndReqCb_t upgradeEndReqCbFunc;
    zcl_ota_upgradeEndRspCb_t upgradeEndRspCbFunc;
    zcl_ota_queryDevSpecFileReqCb_t queryDevSpecFileReqCbFunc;
    zcl_ota_queryDevSpecFileRspCb_t queryDevSpecFileRspCbFunc;
} zcl_ota_AppCallbacks_t;


/**
 *  @brief  External variable for OTA Attribute
 */
extern addrExt_t zcl_attr_upgradeServerID;
extern uint32_t zcl_attr_fileOffset;
extern uint32_t zcl_attr_currFileVer;
extern uint16_t zcl_attr_currZigbeeStackVer;
extern uint32_t zcl_attr_downloadFileVer;
extern uint16_t zcl_attr_downloadZigbeeStackVer;
extern uint8_t zcl_attr_imageUpgradeStatus;
extern uint16_t zcl_attr_manufacturerID;
extern uint16_t zcl_attr_imageTypeID;
extern uint16_t zcl_attr_minBlockPeriod;//in seconds
//extern uint32_t zcl_attr_imageStamp;

/**
 *  @brief  External variable for OTA Attribute info table
 */
extern const zclAttrInfo_t ota_attrTbl[];
extern const uint8_t zcl_ota_attrNum;
#endif

status_t zcl_ota_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

status_t zcl_ota_imageNotifySend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_imageNotify_t *pReq);
#define zcl_ota_imageNotifyCmdSend(a,b,c,d)             (zcl_ota_imageNotifySend((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_ota_queryNextImageReqSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_queryNextImageReq_t *pReq);
#define zcl_ota_queryNextImageReqCmdSend(a,b,c,d)       (zcl_ota_queryNextImageReqSend((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_ota_queryNextImageRspSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_queryNextImageRsp_t *pRsp);
#define zcl_ota_queryNextImageRspCmdSend(a,b,c,d,e)     (zcl_ota_queryNextImageRspSend((a), (b), (c), (d), (e)))

status_t zcl_ota_imageBlockReqSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_imageBlockReq_t *pReq);
#define zcl_ota_imageBlockReqCmdSend(a,b,c,d)           (zcl_ota_imageBlockReqSend((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_ota_imagePageReqSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_imagePageReq_t *pReq);

status_t zcl_ota_imageBlockRspSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_imageBlockRsp_t *pRsp);
#define zcl_ota_imageBlockRspCmdSend(a,b,c,d,e)         (zcl_ota_imageBlockRspSend((a), (b), (c), (d), (e)))

status_t zcl_ota_upgradeEndReqSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_upgradeEndReq_t *pReq);
#define zcl_ota_upgradeEndReqCmdSend(a,b,c,d)           (zcl_ota_upgradeEndReqSend((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_ota_upgradeEndRspSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_upgradeEndRsp_t *pRsp);
#define zcl_ota_upgradeEndRspCmdSend(a,b,c,d,e)         (zcl_ota_upgradeEndRspSend((a), (b), (c), (d), (e)))

status_t zcl_ota_queryDevSpecFileReqSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_querySpecFileReq_t *pReq);

status_t zcl_ota_queryDevSpecFileRspSend(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, ota_querySpecFileRsp_t *pRsp);


#endif	/* ZCL_OTA_H */
