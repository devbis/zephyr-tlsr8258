/********************************************************************************************************
 * @file    tl_zb_mac.h
 *
 * @brief   This is the header file for tl_zb_mac
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
#ifndef ZB_MAC_H
#define ZB_MAC_H


/** @addtogroup  TELINK_ZIGBEE_STACK TELINK ZigBee Stack
 *  @{
 */

/** @addtogroup  TELINK_ZIGBEE_MAC telink mac
 *  @{
 */
#define MAX_PHY_FRM_SIZE                        127

/**
 *  @brief Broadcast PAN ID
 */
#define MAC_SHORT_ADDR_MAX                      0xFFF7
#define MAC_PAN_ID_BROADCAST                    0xFFFF
#define MAC_ADDR_USE_EXT                        0xFFFE //!< Short address value indicating extended address is used
#define MAC_SHORT_ADDR_BROADCAST                0xFFFF //!< Broadcast short address
#define MAC_SHORT_ADDR_NONE                     0xFFFF //!< Short address when there is no short address

/**
 *  @brief Key source length definition in Mac security
 */
#define MAC_KEY_SOURCE_MAX_LEN                  8

/**
 *  @brief Define the max number of ED scan results supported
 */
#define MAX_ED_SCAN_RESULTS_SUPPORTED           16

/**
 *  @brief Define the max number of PAN descriptors supported
 */
#define MAX_PAN_DESC_SUPPORTED                  6

/* MAC security */
#ifdef ZB_MAC_SECURITY
#define MAC_SECUR_LEV5_KEYID1_AUX_HDR_SIZE      6
#else
#define MAC_SECUR_LEV5_KEYID1_AUX_HDR_SIZE      0
#endif

#define ZB_MAC_SECURITY_LEVEL                   5
#define ZB_MAC_KEY_ID_MODE                      1

#define ZB_TURN_OFF_ORDER                       15

#define ZB_MAC_SHORT_ADDR_NOT_ALLOCATED         0xFFFE

/**
   Defines scan duration
 */
#define ZB_MAX_SCAN_DURATION_VALUE              200


#define ZB_MAC_PENDING_TRANS_QUEUE_NUM          6
/* According to 802.15.4, should be 0. */
#define ZB_MAC_INTERNAL_EXPIRY_CNT              0


#define ZB_MAC_CHANNEL_START                 11
#define ZB_MAC_CHANNEL_NUM                   16
#define ZB_MAC_CHANNEL_STOP                  (ZB_MAC_CHANNEL_START + ZB_MAC_CHANNEL_NUM - 1)

#define MAGIC_ROUND_TO_4(a)                     (((a) + ((4)-1)) & -(4))


/** @addtogroup mac_primitive_id MAC primitive ID
 * Primitive ID of MAC request/response/indication/confirmation
 * @{
 */
#define MAC_MCPS_DATA_REQ                       0x40 //!< MCPS Data Request
#define MAC_MCPS_DATA_CNF                       0x41 //!< MCPS Data Confirm
#define MAC_MCPS_DATA_IND                       0x42 //!< MCPS Data Indication
#define MAC_MCPS_PURGE_REQ                      0x43 //!< MCPS Purge request
#define MAC_MCPS_PURGE_CNF                      0x44 //!< MCPS Purge Confirm
#define MAC_MLME_ASSOCIATE_REQ                  0x45 //!< MLME Associate Request
#define MAC_MLME_ASSOCIATE_CNF                  0x46 //!< MLME Associate Confirm
#define MAC_MLME_ASSOCIATE_IND                  0x47 //!< MLME Associate Indication
#define MAC_MLME_ASSOCIATE_RES                  0x48 //!< MLME Associate Response
#define MAC_MLME_DISASSOCIATE_REQ               0x49 //!< MLME Disassociate Request
#define MAC_MLME_DISASSOCIATE_CNF               0x4a //!< MLME Disassociate Confirm
#define MAC_MLME_DISASSOCIATE_IND               0x4b //!< MLME Disassociate Indication
#define MAC_MLME_BEACON_NOTIFY_IND              0x4c //!< MLME Beacon Notify Indication
#define MAC_MLME_GET_REQ                        0x4d //!< MLME Get Request
#define MAC_MLME_GET_CNF                        0x4e //!< MLME Get Confirm
#define MAC_MLME_ORPHAN_IND                     0x4f //!< MLME Orphan Indication
#define MAC_MLME_ORPHAN_RES                     0x50 //!< MLME Orphan Response
#define MAC_MLME_RESET_REQ                      0x51 //!< MLME Reset Request
#define MAC_MLME_RESET_CNF                      0x52 //!< MLME Reset Confirm
#define MAC_MLME_RX_ENABLE_REQ                  0x53 //!< MLME RX Enable Request
#define MAC_MLME_RX_ENABLE_CNF                  0x54 //!< MLME RX Enable Confirm
#define MAC_MLME_SCAN_REQ                       0x55 //!< MLME Scan Request
#define MAC_MLME_SCAN_CNF                       0x56 //!< MLME Scan Confirm
#define MAC_MLME_COMM_STATUS_IND                0x57 //!< MLME Comm Status Indication
#define MAC_MLME_SET_REQ                        0x58 //!< MLME Set Request
#define MAC_MLME_SET_CNF                        0x59 //!< MLME Set Confirm
#define MAC_MLME_START_REQ                      0x5a //!< MLME Start Request
#define MAC_MLME_START_CNF                      0x5b //!< MLME Start Confirm
#define MAC_MLME_SYNC_LOSS_IND                  0x5c //!< MLME Sync Loss Indication
#define MAC_MLME_POLL_REQ                       0x5d //!< MLME Poll Request
#define MAC_MLME_POLL_CNF                       0x5e //!< MLME Poll Confirm
#define MAC_MLME_SCAN_COMPLETE                  0x5f //!< internal use, MLME Scan Complete
#define MAC_MLME_BEACON_REQ_CMD                 0x60 //!< internal use, Receive Beacon Request Command
#define MAC_MLME_DATA_REQ_CMD                   0x61 //!< internal use, Receive Data Request Command
#define MAC_MLME_ASSO_RSP_CNF                   0x62 //!< internal use, Receive Associate Response Confirm
#define MAC_MLME_POLL_IND                       0x63 //!< MLME Poll Indication for zigBee 3.0
/** @} end of group mac_mcsp_data_primitive */


/* MAC header frame control */
#define MAC_FCF_ACK_REQ_BIT                     0x20
#define MAC_FCF_INTRA_PAN_BIT                   0x40
#define MAC_FCF_DST_ADDR_BIT                    0x0c
#define MAC_FCF_SRC_ADDR_BIT                    0xc0
#define MAC_FCF_FRAME_TYPE                      0x07

enum mac_frame_version_e {
    MAC_FRAME_IEEE_802_15_4_2003                = 0,
    MAC_FRAME_IEEE_802_15_4                     = 1
};

typedef enum zb_addr_mode_e {
    ZB_ADDR_NO_ADDR                             = 0,/*!< 802.15: 0x00 = no address (addressing fields omitted, see 7.2.1.1.8). */
    ZB_ADDR_16BIT_MULTICAST                     = 1,/*!< 802.15: 0x1 = reserved.
                                                     * NWK: 0x01 = 16-bit multicast group address
                                                     * APS: 0x01 = 16-bit group address for DstAddress;
                                                     */
    ZB_ADDR_16BIT_GROUP                         = 1,
    ZB_ADDR_16BIT_DEV_OR_BROADCAST              = 2,/*!< 802.15: 0x02 = 16-bit short address.
                                                     * NWK: 0x02=16-bit network address of a device or a 16-bit broadcast address
                                                     */
    ZB_ADDR_64BIT_DEV                           = 3 /*!< 802.15: 0x03 = 64-bit extended address. */
} zb_addr_mode_t;

/**
   Device types
 */
enum {
    MAC_DEV_UNDEFINED,
    MAC_DEV_FFD,
    MAC_DEV_RFD
};

/**
   MAC options bits
 */
enum mac_tx_options_bits_e {
    MAC_TX_OPTION_ACKNOWLEDGED_BIT              = 1,
    MAC_TX_OPTION_GTS_CAP_BIT                   = 2,
    MAC_TX_OPTION_INDIRECT_TRANSMISSION_BIT     = 4,
    MAC_TX_OPTION_USE_CSMA_BIT                  = 8,//add for green power
};

/**
   MAC frame types
 */
enum mac_frame_type_e {
    MAC_FRAME_BEACON                            = 0,
    MAC_FRAME_DATA                              = 1,
    MAC_FRAME_ACKNOWLEDGMENT                    = 2,
    MAC_FRAME_COMMAND                           = 3,
    MAC_FRAME_RESERVED1                         = 4,
    MAC_FRAME_RESERVED2                         = 5,
    MAC_FRAME_RESERVED3                         = 6,
    MAC_FRAME_RESERVED4                         = 7
};


/** @addtogroup zb_mac_attribute_id Zigbee Mac attributes Identifier
 *  Definition for zigbee Mac attributes Identifier
 *  @{
 */
enum {
    MAC_PHY_ATTR_CURRENT_CHANNEL                = 0x00,
    MAC_PHY_ATTR_MODE_CCA                       = 0x03,
    MAC_PHY_ATTR_CURRENT_PAGE                   = 0x04,

    MAC_PIB_ATTRIBUTE_START                     = 0x40, //!<
    MAC_ATTR_ACK_WAIT_DURATION                  = 0x40, //!< attribute Identifier: macAckWaitDuration
    MAC_ATTR_ASSOCIATION_PERMIT                 = 0x41, //!< attribute Identifier: macAssociationPermit
    MAC_ATTR_AUTO_REQUEST                       = 0x42, //!< attribute Identifier: macAutoRequest
    MAC_ATTR_BATTERY_LIFE_EXT                   = 0x43, //!< attribute Identifier: macBattLifeExt
    MAC_ATTR_BATTERY_LIFE_EXT_PERIODS           = 0x44, //!< attribute Identifier: macBattLifeExtPeriods
    MAC_ATTR_BEACON_PAYLOAD                     = 0x45, //!< attribute Identifier: macBeaconPayload
    MAC_ATTR_BEACON_PAYLOAD_LENGTH              = 0x46, //!< attribute Identifier: macBeaconPayloadLength
    MAC_ATTR_BEACON_ORDER                       = 0x47, //!< attribute Identifier: macBeaconOrder
    MAC_ATTR_BEACON_TX_TIME                     = 0x48, //!< attribute Identifier: macBeaconTxTime
    MAC_ATTR_BSN                                = 0x49, //!< attribute Identifier: macBSN
    MAC_ATTR_COORDINATOR_EXTENDED_ADDRESS       = 0x4A, //!< attribute Identifier: macCoordExtendedAddress
    MAC_ATTR_OORDINATOR_SHORT_ADDRESS           = 0x4B, //!< attribute Identifier: macCoordShortAddress
    MAC_ATTR_DSN                                = 0x4C, //!< attribute Identifier: macDSN
    MAC_ATTR_GTS_PERMIT                         = 0x4D, //!< attribute Identifier: macGTSPermit
    MAC_ATTR_MAX_CSMA_BACKOFFS                  = 0x4E, //!< attribute Identifier: macMaxCSMABackoffs
    MAC_ATTR_MIN_BE                             = 0x4F, //!< attribute Identifier: macMinBE
    MAC_ATTR_PAN_ID                             = 0x50, //!< attribute Identifier: macPANId
    MAC_ATTR_PROMISCUOUS_MODE                   = 0x51, //!< attribute Identifier: macPromiscuousMode
    MAC_ATTR_RX_ON_WHEN_IDLE                    = 0x52, //!< attribute Identifier: macRxOnWhenIdle
    MAC_ATTR_SHORT_ADDRESS                      = 0x53, //!< attribute Identifier: macShortAddress
    MAC_ATTR_SUPERFRAME_ORDER                   = 0x54, //!< attribute Identifier: macSuperframeOrder
    MAC_ATTR_TRANSACTION_PERSISTENCE_TIME       = 0x55, //!< attribute Identifier: macTransactionPersistenceTime
    MAC_ATTR_PAN_COORDASSOCIATED                = 0x56, //!< attribute Identifier: macAssociatedPANCoord
    MAC_ATTR_MAX_BE                             = 0x57, //!< attribute Identifier: macMaxBE
    MAC_ATTR_MAX_FRAME_TOTAL_WAIT_TIME          = 0x58, //!< attribute Identifier: macMaxFrameTotalWaitTime
    MAC_ATTR_MAX_FRAME_RETRIES                  = 0x59, //!< attribute Identifier: macMaxFrameRetries
    MAC_ATTR_RESPONSE_WAIT_TIME                 = 0x5A, //!< attribute Identifier: macMaxFrameRetries
    MAC_ATTR_SYNC_SYMBOL_OFFSET                 = 0x5B, //!< attribute Identifier: macSyncSymbolOffset
    MAC_ATTR_TIMESTAMP_SUPPORTED                = 0x5C, //!< attribute Identifier: macTimestampSupported
    MAC_ATTR_SECURITY_ENABLED                   = 0x5D, //!< attribute Identifier: macSecurityEnabled

    MAC_CONST_EXT_ADDR                          = 0x5E,	//!< attribute Identifier: mac ieee address, internal used only
    MAC_TX_POWER                                = 0x5F,	//!< attribute Identifier: tx power, internal used only
    MAC_PIB_ATTRIBUTE_STOP                      = 0x5F, //!< attribute Identifier: autoRequest
};

/** @} end of group zb_mac_attribute_id */


/**
   MAC command frame id
 */
enum mac_command_frame_id {
    MAC_CMD_ASSOCIATION_REQUEST                 = 1,
    MAC_CMD_ASSOCIATION_RESPONSE,
    MAC_CMD_DISASSOCIATION_NOTIFICATION,
    MAC_CMD_DATA_REQUEST,
    MAC_CMD_PAN_ID_CONFLICT_NOTIFICATION,
    MAC_CMD_ORPHAN_NOTIFICATION,
    MAC_CMD_BEACON_REQUEST,                     /* 7 */
    MAC_CMD_COORDINATOR_REALIGNMENT,
    MAC_CMD_GTS_REQUEST,

    MAC_CMD_TELINK_BASE                         = 0xA0,
    MAC_CMD_TELINK_OTA,
};

enum {
    ZB_MAC_STATE_NORMAL                         = 0,
    ZB_MAC_STATE_ED_SCAN,
    ZB_MAC_STATE_ACTIVE_SCAN,
    ZB_MAC_STATE_ORPHAN_SCAN,
    ZB_MAC_STATE_PASSIVE_SCAN,
    ZB_MAC_STATE_INDIRECT_DATA,
};

typedef enum {
    MAC_SUCCESS                                 = 0x00,/**< Operation successful */
    MAC_STA_PAN_AT_CAPACITY                     = 0x01,
    MAC_STA_PAN_ACCESS_DENIED                   = 0x02,

    MAC_STA_UNSUPPORTED                         = 0x18,
    MAC_STA_BAD_STATE                           = 0x19,
    MAC_STA_NO_RESOURCES                        = 0x1A,
    MAC_TX_ABORTED                              = 0x1D,/**< For internal use only */
    MAC_STA_FRAME_PENDING                       = 0x20,

    MAC_STA_COUNTER_ERROR                       = 0xDB,
    MAC_STA_IMPROPER_KEY_TYPE                   = 0xDC,
    MAC_STA_IMPROPER_SECURITY_LEVEL             = 0xDD,
    MAC_STA_UNSUPPORTED_LEGACY                  = 0xDE,
    MAC_STA_UNSUPPORTED_SECURITY                = 0xDF,
    MAC_STA_BEACON_LOSS                         = 0xE0,
    MAC_STA_CHANNEL_ACCESS_FAILURE              = 0xE1,
    MAC_STA_DENIED                              = 0xE2,
    MAC_STA_DISABLE_TRX_FAILURE                 = 0xE3,
    MAC_STA_SECURITY_ERROR                      = 0xE4,
    MAC_STA_FRAME_TOO_LONG                      = 0xE5,
    MAC_STA_INVALID_GTS                         = 0xE6,
    MAC_STA_INVALID_HANDLE                      = 0xE7,
    MAC_STA_INVALID_PARAMETER                   = 0xE8,
    MAC_STA_NO_ACK                              = 0xE9,
    MAC_STA_NO_BEACON                           = 0xEA,
    MAC_STA_NO_DATA                             = 0xEB,
    MAC_STA_NO_SHORT_ADDRESS                    = 0xEC,
    MAC_STA_OUT_OF_CAP                          = 0xED,
    MAC_STA_PAN_ID_CONFLICT                     = 0xEE,
    MAC_STA_REALIGNMENT_GOT                     = 0xEF,
    MAC_STA_TRANSACTION_EXPIRED                 = 0xF0,
    MAC_STA_TRANSACTION_OVERFLOW                = 0xF1,
    MAC_STA_TX_ACTIVE                           = 0xF2,
    MAC_STA_KEY_UNAVAILABLE                     = 0xF3,
    MAC_STA_UNSUPPORTED_ATTRIBUTE               = 0xF4,
    MAC_STA_INVALID_ADDRESS                     = 0xF5,
    MAC_STA_ON_TIME_TOO_LONG                    = 0xF6,
    MAC_STA_PAST_TIME                           = 0xF7,
    MAC_STA_TRACKING_OFF                        = 0xF8,
    MAC_STA_INVALID_INDEX                       = 0xF9,
    MAC_STA_LIMIT_REACHED                       = 0xFA,
    MAC_STA_READ_ONLY                           = 0xFB,
    MAC_STA_IN_SCAN_DOING                       = 0xFC,
    MAC_STA_SUPERFRAME_OVERLAP                  = 0xFD,
    MAC_STA_AUTOACK_PENDING_ALL_ON              = 0xFE,
    MAC_STA_AUTOACK_PENDING_ALL_OFF             = 0xFF,
} mac_sts_t;


/**
   Enum defines types of possible scan operations

   @param ED_SCAN
   @param ACTIVE_SCAN
   @param PASSIVE_SCAN
   @param ORPHAN_SCAN
*/
enum zb_mac_scan_type_e {
    ED_SCAN                                     = 0,
    ACTIVE_SCAN                                 = 1,
    PASSIVE_SCAN                                = 2,
    ORPHAN_SCAN                                 = 3
};

enum {
    ZB_MAC_DISASSOCIATE_REASON_RESV             = 0,
    ZB_MAC_DISASSOCIATE_REASON_COORD2DEV        = 1,
    ZB_MAC_DISASSOCIATE_REASON_DEV              = 2,
};


/* Shared with NWK headers; guard to avoid duplicate typedefs. */
#ifndef ZB_CAPABILITY_INFO_T_DEFINED
#define ZB_CAPABILITY_INFO_T_DEFINED
typedef struct {
    uint8_t altPanCoord:1;
    uint8_t devType:1;
    uint8_t powerSrc:1;
    uint8_t rcvOnWhenIdle:1;
    uint8_t reserved:2;
    uint8_t secuCapability:1;
    uint8_t allocAddr:1;
} capability_info_t;
#endif

/**
 *  @brief  Definition for Common security type
 */
typedef struct {
    uint8_t keySource[MAC_KEY_SOURCE_MAX_LEN];  //!< Key source
    uint8_t securityLevel;                      //!< Security level
    uint8_t keyIdMode;                          //!< Key identifier mode
    uint8_t keyIndex;                           //!< Key index
} mac_sec_t;

/**
 *  @brief  Definition of PAN descriptor type
 */
typedef struct {
    uint32_t timestamp;              //!< The time at which the beacon frame was received, in symbols

    uint16_t coordPanId;             //!< PAN identifier of the coordinator
    uint16_t superframeSpec;         //!< Super frame specification as specified in the received beacon frame

    addr_t coordAddr;           //!< Coordinator address
    mac_sec_t sec;              //!< Security parameters
    uint8_t logicalChannel;          //!< The current logical channel occupied by the network

    uint8_t channelPage;             //!< The current channel page occupied by the network
    uint8_t gtsPermit;               //!< PAN coordinator is accepting GTS requests or not
    uint8_t linkQuality;             //!< The LQI at which the network beacon was received

    uint8_t securityFailure;         //!< Set to TRUE if there was an error in the security processing
    uint8_t rsv[3];
} mac_panDesc_t;

/** @defgroup mac_mcsp_data_primitive MAC-MCSP:DATA primitive Types
 *  @{
 */
typedef struct {
    uint16_t dstPanId;
    addr_t srcAddr;
    addr_t dstAddr;
    uint8_t msduLength;
    uint8_t msduHandle;
    uint8_t *msdu;
    uint8_t txOptions;
#ifdef ZB_MAC_SECURITY
    uint8_t securityLevel;
    uint8_t keyIdMode;
    uint8_t keyIndex;
    uint8_t keySource[MAC_KEY_SOURCE_MAX_LEN];
#endif
} zb_mscp_data_req_t;

/** @defgroup mac_mcsp_data_primitive MAC-MCSP:confirm primitive Types
 *  @{
 */
typedef struct {
    uint32_t timestamp;
    uint8_t *msdu;
    uint8_t msduHandle;
    uint8_t status;
    uint16_t macDstAddr;
    int8_t rssi;
    uint8_t lqi;
} zb_mscp_data_conf_t;

/** @defgroup mac_mcsp_data_primitive MAC-MCSP:indication primitive Types
 *  @{
 */
typedef struct {
    uint32_t timestamp;
    uint8_t *msdu;
    uint16_t srcPanId;
    uint16_t dstPanId;
    addr_t srcAddr;
    addr_t dstAddr;
    uint8_t msduLength;
    uint8_t mpduLinkQuality;
    uint8_t dsn;
#ifdef ZB_MAC_SECURITY
    uint8_t securityLevel;
    uint8_t keyIdMode;
    uint8_t keyIndex;
    uint8_t keySource[MAC_KEY_SOURCE_MAX_LEN];
#endif
} zb_mscp_data_ind_t;

/** @defgroup mac_mcsp_purge_primitive MAC-MCSP:PURGE primitive Types
 *  @{
 */
typedef struct {
    uint8_t msduHandle;      /*!< The handle of the MSDU requested to be purge from the transaction queue. */
} zb_mscp_purge_req_t;

typedef struct {
    uint8_t msduHandle;      /*!< The handle of the MSDU requested to be purge from the transaction queue. */
    uint8_t status;          /*!< SUCCESS or INVALID_HANDLE*/
} zb_mscp_purge_conf_t;
/** @} end of group mac_mcsp_purge_primitive */

/** @defgroup mac_mlme_associate_primitive MAC-MLME:ASSOCIATE primitive Types
 *  @{
 */
typedef struct {
    uint8_t logicalChannel;
    uint8_t channelPage;
    uint16_t coordPanId;

    addr_t coordAddress;
    capability_info_t capbilityInfo;
    mac_sec_t sec;
} zb_mlme_associate_req_t;

typedef struct {
    addrExt_t devAddress;
    capability_info_t capbilityInfo;
    uint8_t lqi;
    mac_sec_t sec;
} zb_mlme_associate_ind_t;

typedef struct {
    uint16_t shortAddress;
    addrExt_t devAddress;
    uint8_t status;
    mac_sec_t sec;
} zb_mlme_associate_resp_t;

typedef struct {
    addrExt_t parentAddress;
    uint16_t shortAddress;
    uint8_t status;
    mac_sec_t sec;
} zb_mlme_associate_conf_t;
/** @} end of group mac_mlme_associate_primitive */


/** @defgroup mac_mlme_disassociate_primitive MAC-MLME:DISASSOCIATE primitive Types
 *  @{
 */
typedef struct {
    uint16_t devPanId;
    addr_t devAddr;
    uint8_t disassociateReason;
    bool txIndirect;
    mac_sec_t sec;
} zb_mlme_disassociate_req_t;

typedef struct {
    addrExt_t devAddress;
    uint8_t disassociateReason;
    mac_sec_t sec;
} zb_mlme_disassociate_ind_t;

typedef struct {
    uint16_t devPanId;
    addr_t devAddr;
    uint8_t status;
} zb_mlme_disassociate_conf_t;
/** @} end of group mac_mlme_disassociate_primitive */


/**
 *  @brief  Define MLME beacon notify indication type
 */
typedef struct {
    mac_panDesc_t panDesc;      //!< The PAN Descriptor for the received beacon
    uint8_t *pAddrList;              //!< The list of device addresses for which the sender of the beacon has data
    uint8_t *psdu;                   //!< The beacon payload
    uint8_t bsn;                     //!< Beacon Sequence Number
    uint8_t pendAddrSpec;            //!< The beacon pending address specification
    uint8_t psduLength;              //!< Number of octets contained in the beacon payload of the beacon frame
    uint8_t resved;                  //!< for align-4
} zb_mlme_beacon_notify_ind_t;


/**
   MLME-COMM-STATUS.indication data structure

   Not all fields included.
   Currently used after MLME-ASSOCIATE.response only.
   When will be used for MLME-ORPHAN.response, maybe, will add some fields.
 */
typedef struct {
    uint16_t panId;
    addr_t srcAddr;
    addr_t dstAddr;
    uint8_t status;
    bool isAssoc;//TRUE is associate, FALSE is orphan
    mac_sec_t sec;
} zb_mlme_comm_status_ind_t;

/**
 *  @brief  Define MLME Request type
 */
typedef struct {
    bool setAsDefault;  //!< set as default: 0 or 1
} mac_mlme_reset_req_t;

/**
 *  @brief  Define MLME Confirm type
 */
typedef struct {
    uint8_t status;  //!< The status of the reset @ref mac_status_id
} mac_mlme_reset_conf_t;

/**
 *  @brief  Define MLME Rx-On enable request type
 */
typedef struct {
    uint8_t primitive;       //!< Primitive id of MLME Rx-On Enable Request @ref mac_primitive_id
    uint32_t rxOnTime;       //!< The Number of symbols measured before the receiver is to be enabled
    mac_sec_t sec;      //!< The security parameters for this message
} mac_mlme_rxOnEnableReq_t;

/**
 *  @brief  Define MLME Rx-On enable confirm type
 */
typedef struct {
    uint8_t primitive;       //!< Primitive id of MLME Rx-on Enable Confirm @ref mac_primitive_id
    uint8_t status;          //!< The status of the data request @ref mac_status_id
} mac_mlme_rxOnEnableCnf_t;

/**
 *  @brief  Define MLME scan request type
 */
typedef struct {
    uint32_t scanChannels;   //!< Indicate which channels are to be scanned
    uint8_t scanType;        //!< The type of scan to be performed @ref mac_scan_type
    uint8_t scanDuration;    //!< The time spent on scanning each channel
    uint8_t channelPage;     //!< Channel page on which to perform the scan
    mac_sec_t sec;      //!< The security parameters for orphan scan
} zb_mac_mlme_scan_req_t;

/**
 *  @brief  Define MLME scan confirm type
 */
typedef struct {
    uint8_t status;                  //!< Status of the scan request @ref mac_status_id
    uint8_t scanType;                //!< The type of scan performed @ref mac_scan_type
    uint8_t channelPage;             //!< Channel page on which the scan was performed
    uint8_t resultListSize;          //!< Number of elements returned in the appropriate result lists


    uint32_t unscannedChannels;      //!< Channels given in the request which were not scanned

    union {                     //!< Define a union of energy measurements list and pan descriptor list
        uint8_t energyDetectList[MAX_ED_SCAN_RESULTS_SUPPORTED];     //!< List of energy measurements
        mac_panDesc_t panDescList[MAX_PAN_DESC_SUPPORTED];      //!< List of PAN descriptors
    } resultList;
} zb_mac_mlme_scan_conf_t;

/**
 *  @brief  Define MLME start request type
 */
typedef struct {
    uint32_t startTime;              //!< Time at which to begin transmitting beacons

    uint16_t panId;                  //!< PAN identifier to be used by the device
    uint8_t logicalChannel;          //!< Logical channel on which to begin
    uint8_t channelPage;             //!< Channel page on which to begin

    uint8_t beaconOrder;             //!< Indicates how often the beacon is to be transmitted
    uint8_t superframeOrder;         //!< Length of the active portion of the superframe
    uint8_t panCoordinator;          //!< Length of the active portion of the superframe
    uint8_t batteryLifeExt;          //!< Indicates if the receiver of the beaconing device is disabled or not

    mac_sec_t coordRealignSec;  //!< Security parameters for the coordinator realignment frame
    uint8_t coordRealignment;        //!< Indicates if the coordinator realignment command is to be transmitted

    mac_sec_t beaconSec;        //!< Security parameters for the beacon frame
} zb_mac_mlme_start_req_t;


/**
 *  @brief  Define MLME start confirm type
 */
typedef struct {
    uint8_t status;  //!< Result of the attempt to start using an updated superframe configuration @ref mac_status_id
} mac_mlme_startCnf_t;

/**
 *  @brief  Define MLME poll request type
 */
typedef struct {
    uint16_t coordPanId;
    zb_addr_t coordAddr;
    uint8_t coordAddrMode;
} mac_mlme_poll_req_t;

/**
 *  @brief  Define MLME poll confirm type
 */
typedef struct {
    uint8_t status;  //!< Result of the attempt to start using an updated superframe configuration @ref mac_status_id
} mac_mlme_poll_conf_t;

/**
 *  @brief  Define MLME poll indication type
 */
typedef struct {
    uint8_t addrMode;                /*<! 2 - 16 bit short address. 3 - 64 bit extended address */
    zb_addr_t devAddr;       /*<! The address of the device requesting pending data */
} mac_mlme_poll_ind_t;

/**
   Parameters for orphan indication
*/
typedef struct {
    addrExt_t orphanAddr;       /*<! The 64 bits address of the orphaned device */
} mac_mlme_orphan_ind_t;

/**
   Parameters for orphan response
*/
typedef struct {
    addrExt_t orphanAddr;       /*<! The 64 bits address of the orphaned device */
    uint16_t shortAddr;              /*<! The 16-bit short address allocated to the
                                 * orphaned device if it is associated with this coordinator */
    bool associated;            /*<! TRUE if the orphaned device is associated with this
                                 * coordinator or FALSE otherwise */
} mac_mlme_orphan_resp_t;

/**
   Sync loss reasons
   The reason that synchronization was lost.
 */
typedef enum {
    ZB_SYNC_LOSS_REASON_PAN_ID_CONFLICT,
    ZB_SYNC_LOSS_REASON_REALIGNMENT,
    ZB_SYNC_LOSS_REASON_BEACON_LOST
} zb_sync_loss_reason_t;

/**
   Parameters for sync loss indication
*/
typedef struct {
    uint16_t panId;                  /* The PAN identifier with which the device lost
                                 * synchronization or to which it was realigned */
    uint8_t reason;                  /* zb_sync_loss_reason_t, Lost syncronisation reason */
    uint8_t logicalChannel;          /* Logical channel */
    uint8_t channelPage;             /* Channel page */
} zb_mlme_sync_loss_ind_t;

typedef struct {
    uint8_t srcAddrMode;
    uint8_t dstAddrMode;
    zb_addr_t srcAddr;
    zb_addr_t dstAddr;
    uint8_t cbType;
} zb_mlme_data_req_cmd_t;

typedef struct {
    uint32_t timestamp;
    uint8_t *psdu;
    uint16_t srcPanId;
    addr_t srcAddr;
    uint8_t len;
    uint8_t lqi;
    uint8_t curChannel;
} zb_mac_raw_frame_t;


#define ZB_MAC_GTS_DESCROPTION_NUM_MAX  7

typedef struct {
    uint8_t beaconOrder:4;
    uint8_t superframeOrder:4;
    uint8_t finalCapSlot:4;
    uint8_t batteryLifeExtension:1;
    uint8_t resv:1;
    uint8_t panCoordinator:1;
    uint8_t associatePermit:1;
} zb_superframe_spec_t;

typedef struct {
    uint8_t descCount:3;
    uint8_t resv:4;
    uint8_t permit:1;
} zb_gts_spec_t;

typedef struct {
    uint8_t shortAddr[2];
    uint8_t startSlot:4;
    uint8_t len:4;
} zb_gts_desc_t;

typedef struct {
    zb_gts_spec_t spec;
    uint8_t direction;
    zb_gts_desc_t desc[ZB_MAC_GTS_DESCROPTION_NUM_MAX];
} zb_mac_gts_t;

typedef struct {
    uint8_t shortPendingAddrNum:3;
    uint8_t resv0:1;
    uint8_t extPendingAddrNum:3;
    uint8_t resv1:1;
} zb_pending_addr_spec_t;

typedef struct {
    zb_pending_addr_spec_t spec;
    uint8_t direction;
    zb_gts_desc_t desc[ZB_MAC_GTS_DESCROPTION_NUM_MAX];
} zb_mac_pending_addr_t;

/*
 * beacon payload
 * */
typedef struct {
    uint8_t protocol_id;
    uint8_t stack_profile:4;
    uint8_t protocol_version:4;

    uint8_t long_uptime:1;           //wwah, has been up for more than 24 hours
    uint8_t tc_connectivity:1;       //wwah, active communication path to TC
    uint8_t router_capacity:1;
    uint8_t device_depth:4;
    uint8_t end_device_capacity:1;

    extPANId_t extended_panid;
    uint8_t txoffset[3];
    uint8_t nwk_update_id;
} zb_mac_beacon_payload_t;

typedef struct {
    uint8_t cmdId;
} zb_rfpkt_beacon_req_t;

typedef struct {
    zb_superframe_spec_t superFrameSpec;
    zb_mac_gts_t gts;
    zb_mac_pending_addr_t pendingAddr;
    zb_mac_beacon_payload_t beaconPayload;
} zb_rfpkt_beacon_t;

/*
 * status for processing the indirect data
 */
enum {
    ZB_INDIRECT_IDLE,
    ZB_INDIRECT_ASSOCIATE,
    ZB_INDIRECT_POLL,
    ZB_INDIRECT_ASSOCIATE_RESPONSE,
    ZB_INDIRECT_MCPS_DATA,
    ZB_INDIRECT_DISASSOCIATE_NOTIFY
};



/**
   define RF transmit callback function
*/
typedef void (*zb_max_tx_conf_cb)(void *arg, uint8_t status);

typedef struct {
    void *arg;
    zb_max_tx_conf_cb cb;
} zb_max_tx_conf_t;

/**
   Parameters for storing data in a pending transaction
*/
typedef struct mac_indirPendingList_t {
    struct mac_indirPendingList_t *next;
    void *data;
    addr_t dstAddr;
    uint8_t expiry;
    uint8_t extExpiryCnt;
    uint8_t state;
    uint8_t status;
} mac_indirPendingList_t;

enum {
    MAC_PENDIND_READY           = 0x1,
    MAC_PENDIND_TX_READY        = 0x2,
    MAC_PENDIND_TXING           = 0x3,
    MAC_PENDIND_EXPIRY          = 0x4,
};

/**
   Parameters for storing data in a pending queue
*/
typedef struct zb_mac_pending_data_s {
    zb_buf_t *pending_data;
    uint8_t dst_addr_mode;//zb_addr_mode_t
    zb_addr_t dst_addr;
} zb_mac_pending_data_t;

/**
   Parameters for MAC layer
*/
typedef struct {
    uint8_t warmStart;               /*!< The restart mode of the MAC layer. */
    uint8_t status;                  /*!< The status of the MAC layer. */
    uint8_t curChannel;              /*!< The selected channel of the MAC layer. */
    uint8_t beaconTriesNum;

    uint8_t *txRawDataBuf;

    uint8_t indirectData;
} zb_mac_ctx_t;

/**
   the struct for parsing MAC header
*/
typedef struct {
    uint16_t frameCtrl;
    uint8_t dstAddrMode;
    uint8_t srcAddrMode;
    uint8_t panIdMode;
    uint8_t seqNum;
    uint16_t dstPanId;
    zb_addr_t dstAddr;
    uint16_t srcPanId;
    zb_addr_t srcAddr;
#ifdef ZB_MAC_SECURITY
    uint8_t secLevel;
    uint8_t keyIdMode;
    uint8_t keySrc[8];
    uint8_t keyIdx;
    uint32_t framCounter;
#endif
} zb_mac_mhr_t;

/**
   the format of the beacon packet
*/
typedef struct {
    uint8_t srcAddrMode;
    uint8_t secEnable;
    uint8_t framePending;
    uint8_t ble;
    uint8_t beaconOrder;
    uint16_t superframeOrder;
} zb_BeaconFrame_t;

/**
   Parameters for sending data request
*/
typedef enum {
    MAC_ASS_CONFIRM_CALLBACK    = 0,
    MAC_POLL_REQUEST_CALLBACK   = 1
} zb_callback_type_t;

typedef struct {
    uint16_t shortAddr;
    addrExt_t ieeeAddr;
} access_addr_t;




extern uint8_t ZB_MAC_PENDING_TRANS_QUEUE_SIZE;
extern uint8_t ZB_MAC_EXT_EXPEIRY_CNT;
extern zb_mac_ctx_t g_zbMacCtx;

#define ZB_MAC_STATUS_SET(sta)               (g_zbMacCtx.status = sta)
#define ZB_MAC_STATUS_GET()                  (g_zbMacCtx.status)


/* PIB access macros */
/**
   Get mac short address
 */
#define ZB_PIB_SHORT_ADDRESS()                  (g_zbMacPib.shortAddress)
/**
   Get mac extended address
 */
#define ZB_PIB_EXTENDED_ADDRESS()               (g_zbMacPib.extAddress)
/**
   Get mac rx on when idle
 */
#define ZB_PIB_RX_ON_WHEN_IDLE()                (g_zbMacPib.rxOnWhenIdle)
/**
   Get mac ack wait duration
 */
#define ZB_PIB_ACK_WAIT_DURATION()              (g_zbMacPib.ackWaitDuration)
/**
   Get mac DSN
 */
#define ZB_MAC_DSN()                            (g_zbMacPib.seqNum)
/**
   Get mac pan BSN
 */
#define ZB_MAC_BSN()                            (g_zbMacPib.beaconSeqNum)
/**
   Increment mac pan DSN
 */
#define ZB_INC_MAC_DSN()                        (g_zbMacPib.seqNum++)
/**
   Increment mac pan BSN
 */
#define ZB_INC_MAC_BSN()                        (g_zbMacPib.beaconSeqNum++)
/**
   Get mac beacon payload
 */
#define ZB_PIB_BEACON_PAYLOAD()                 (g_zbMacPib.beaconPayload)





void zb_MacChannelSet(uint8_t chan);

#define ZB_TRANSCEIVER_SET_CHANNEL(chn)         zb_MacChannelSet(chn);


void zb_MacReset(void);

/*
 * MAC layer initialization
 *
 * @param None
 *
 * @return None
 *
 * */
void zb_MacInit(uint8_t coldReset);

/*
 * MAC layer main task procedure
 *
 * @param None
 *
 * @return None
 *
 * */
void zb_MacTaskProc(void);


void zb_MacMcpsDataRequestProc(void *arg);

/*
 * NWK layer to MAC layer primitive
 * */

/*
 * MLME-MCPS-DATA.request primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-MCPS-DATA.request primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacMcpsDataRequest(p)              zb_MacMcpsDataRequestProc(p)

/*
 * MLME-RESET.request primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-RESET.request primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacResetRequest(p)                 zb_PrimitivePost(ZB_Q_NWK2MAC, MAC_MLME_RESET_REQ, p)

/*
 * MLME-POLL.request primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-POLL.request primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacPollRequest(p)                  zb_PrimitivePost(ZB_Q_NWK2MAC, MAC_MLME_POLL_REQ, p)

/*
 * MLME-ASSOCIATE.request primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-ASSOCIATE.request primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacAssociateRequest(p)             zb_PrimitivePost(ZB_Q_NWK2MAC, MAC_MLME_ASSOCIATE_REQ, p)

/*
 * MLME-ASSOCIATE.response primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-ASSOCIATE.response primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define	zb_MacAssociateResponse(p)            zb_PrimitivePost(ZB_Q_NWK2MAC, MAC_MLME_ASSOCIATE_RES, p)

/*
 * MLME-ORPHAN.response primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-ORPHAN.response primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacOrphanResponse(p)               zb_PrimitivePost(ZB_Q_NWK2MAC, MAC_MLME_ORPHAN_RES, p)

/*
 * MLME-SCAN.request primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-SCAN.request primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacScanRequest(p)                  zb_PrimitivePost(ZB_Q_NWK2MAC, MAC_MLME_SCAN_REQ, p)


/*
 * MLME-START.request primitive from upper layer to MAC layer
 *
 * @param p the pointer to the MLME-START.request primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacStartRequest(p)                 zb_PrimitivePost(ZB_Q_NWK2MAC, MAC_MLME_START_REQ, p)


/*
 * MAC layer to NWK layer primitive
 * */

/*
 * MLME-RESET.confirm primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-RESET.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacResetConfirm(p)                 zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_RESET_CNF, p)

/*
* MLME-ASSOCIATE.confirm primitive from MAC layer to upper layer
*
* @param p the pointer to the MLME-ASSOCIATE.confirm primitive and msdu
*
* @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
*
* */
#define zb_MacAssociateConfirm(p)             zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_ASSOCIATE_CNF, p)

/*
 * MLME-DISASSOCIATE.confirm primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-DISASSOCIATE.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacDisassociateConfirm(p)          zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_CNF, p)

/*
* MLME-ASSOCIATE.indiccation primitive from MAC layer to upper layer
*
* @param p the pointer to the MLME-ASSOCIATE.indiccation primitive and msdu
*
* @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
*
* */
#define zb_MacAssociateIndicate(p)            zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_ASSOCIATE_IND, p)

/*
 * MLME-DISASSOCIATE.indiccation primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-DISASSOCIATE.indiccation primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacDisassociateIndicate(p)         zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_DISASSOCIATE_IND, p)

/*
 * MLDE-MCPS-DATA.indiccation primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLDE-MCPS-DATA.indiccation primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacDataIndicate(p)                 zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MCPS_DATA_IND, p)

/*
 * MLDE-MCPS-DATA.confirm primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLDE-MCPS-DATA.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacDataConfirm(p)                  zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MCPS_DATA_CNF, p)

/*
 * MLDE-MCPS-DATA.confirm primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLDE-MCPS-PURGE.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacPurgeConfirm(p)                 zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MCPS_PURGE_CNF, p)

/*
 * MLME-POLL.confirm primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-POLL.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacPollConfirm(p)                  zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_POLL_CNF, p)

/*
 * MLME-POLL.indication primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-POLL.indication primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacPollIndication(p)               zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_POLL_IND, p)

/*
 * MLME-BEACON-NOTIFY.indication primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-BEACON-NOTIFY.indication primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacBeaconNotifyIndicate(p)         zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_BEACON_NOTIFY_IND, p)

/*
 * MLME-ORPHAN.indication primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-ORPHAN.indication primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacOrphanIndicate(p)               zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_ORPHAN_IND, p)

/*
 * MLME-SCAN.confirm primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-SCAN.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacScanConfirm(p)                  zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_SCAN_CNF, p)

/*
 * MLME-COMM-STATUS.indication primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-COMM-STATUS.indication primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacCommStatusIndication(p)         zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_COMM_STATUS_IND, p)

/*
 * MLME-START.confirm primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-START.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacStartConfirm(p)                 zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_START_CNF, p)

/*
 * MLME-SYNC-LOSS.indicaion primitive from MAC layer to upper layer
 *
 * @param p the pointer to the MLME-START.confirm primitive and msdu
 *
 * @return ZB_RET_OVERFLOW if failure, ZB_RET_OK else
 *
 * */
#define zb_MacSyncLossIndication(p)           zb_PrimitivePost(ZB_Q_MAC2NWK, MAC_MLME_SYNC_LOSS_IND, p)




typedef bool (*mac_callback)(void *p);

typedef struct {
    mac_callback macBeaconReqRcvCb;
    mac_callback macBeaconRcvCb;
    mac_callback macAssociationReqRcvCb;
} mac_appIndCb_t;

extern mac_appIndCb_t *macAppIndCb;

void mac_appIndCbRegister(mac_appIndCb_t *cb);

/** @} end of group TELINK_ZIGBEE_STACK */

#endif	/* ZB_MAC_H */
