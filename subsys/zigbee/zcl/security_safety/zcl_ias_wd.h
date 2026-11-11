/********************************************************************************************************
 * @file    zcl_ias_wd.h
 *
 * @brief   This is the header file for zcl_ias_wd
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
#ifndef ZCL_IAS_WD_H
#define ZCL_IAS_WD_H


/*********************************************************************
 * CONSTANTS
 */

/* Command ID */
// Client generated command
#define ZCL_CMD_START_WARNING           0x00
#define ZCL_CMD_SQUAWK                  0x01


/* Attribute ID */
#define ZCL_ATTRID_WD_MAX_DURATION      0x0000


/*********************************************************************
 * TYPES
 */

typedef union {
    uint8_t byte;
    struct {
        uint8_t warningMode:4;
        uint8_t strobe:2;
        uint8_t sirenLevel:2;
    } bits;
} warning_t;

/**
 *  @brief  Structure definition for start warning command
 */
typedef struct {
    uint16_t warningDuration;
    warning_t warning;
    uint8_t strobeDutyCycle;
    uint8_t strobeLevel;
} startWarning_t;

/**
 *  @brief  Structure definition for squawk command
 */
typedef union {
    uint8_t byte;
    struct {
        uint8_t squawk:4;
        uint8_t strobe:1;
        uint8_t reserved:1;
        uint8_t squawkLevel:2;
    } bits;
} squawk_t;

#if 0
/* Command Callback Function Type */
typedef void (*zcl_startWarningCb_t)(apsdeDataInd_t *pApsdeInd, startWarning_t *pStartWarning);
typedef void (*zcl_SquawkCb_t)(apsdeDataInd_t *pApsdeInd, squawk_t *pSquawk);

/**
 *  @brief  Structure definition for commands callback functions in ias wd cluster
 */
typedef struct {
    zcl_startWarningCb_t startWarningCbFunc;
    zcl_SquawkCb_t squawkCbFunc;
} zcl_iasWd_AppCallbacks_t;


extern uint16_t zcl_attr_MaxDuration;

extern const zclAttrInfo_t iasWD_attrTbl[];

extern const uint8_t zcl_iasWD_attrNum;
#endif


status_t zcl_iasWd_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);


status_t zcl_iasWd_startWarning(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, startWarning_t *pStartWarning);
#define zcl_iasWd_startWarningCmd(a,b,c,d)      (zcl_iasWd_startWarning((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_iasWd_squawk(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, squawk_t *pSquawk);
#define zcl_iasWd_squawkCmd(a,b,c,d)            (zcl_iasWd_squawk((a), (b), (c), ZCL_SEQ_NUM, (d)))

#endif	/* ZCL_IAS_WD_H */
