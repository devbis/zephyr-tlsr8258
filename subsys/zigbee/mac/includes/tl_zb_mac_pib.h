/********************************************************************************************************
 * @file    tl_zb_mac_pib.h
 *
 * @brief   This is the header file for tl_zb_mac_pib
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
#ifndef ZB_MAC_PIB_H
#define ZB_MAC_PIB_H


#define	MAC_INVALID_PANID                       0xFFFF

#define MAC_DEVICE_TABLE_SIZE                   4

/*
 * PHY constants
 */
#define ZB_PHY_MAX_PACKET_SIZE                  127
#define ZB_PHY_TURNROUNDTIME                    12
#define	ZB_US_PER_SYMBOL                        16

/*
 * PHY PIB
 */
#define ZB_PHY_SYMBOLS_PER_OCTET                2
#define ZB_PHY_SHR_DURATION                     40
#define ZB_PHY_MAX_FRAME_DURATION               (ZB_PHY_SHR_DURATION + (ZB_PHY_MAX_PACKET_SIZE + 1) * ZB_PHY_SYMBOLS_PER_OCTET)


/** @addtogroup zb_mac_constant Zigbee Mac Constants
 *  Definition for zigbee Mac constants
 *  @{
 */
#define ZB_MAC_BASE_SLOT_DURATION               60    //!< The number of symbols forming a superframe slot when the superframe order is equal to 0
#define ZB_MAC_NUM_SUPERFRAME_SLOTS             16    //!< The number of slots contained in any superframe
#define ZB_MAC_BASE_SUPERFRAME_DURATION         (ZB_MAC_BASE_SLOT_DURATION * ZB_MAC_NUM_SUPERFRAME_SLOTS)
                                                      //!< The number of symbols forming a superframe when the superframe order is equal to 0
#define ZB_MAC_BASE_SUPERFRAME_DURATION_US      (ZB_MAC_BASE_SLOT_DURATION * ZB_MAC_NUM_SUPERFRAME_SLOTS * ZB_US_PER_SYMBOL)
                                                      //!< a superframe(us) when the superframe order is equal to 0
#define ZB_MAC_UNIT_BACKOFF_PERIOD              (20)  //!< The number of symbols forming the basic time period used by the CSMA-CA algorithm.
#define ZB_MAC_UNIT_BACKOFF_PERIOD_US           (ZB_MAC_UNIT_BACKOFF_PERIOD * ZB_US_PER_SYMBOL) //!< the basic time period(us) used by the CSMA-CA algorithm.
#define ZB_MAC_MIN_MPDU_OVERHEAD                (9)   //!< The minimum number of octets added by the MAC sublayer to the PSDU.
#define ZB_MAC_MIN_CAP_LEHGTH                   (440) //!< The minimum number of symbols forming the CAP.
#define ZB_MAC_MAX_SIF_FRAME_SIZE               (18)  //!< The maximum size of an MPDU
#define ZB_MAC_MAX_MPDU_UNSECURITY_OVERHEAD     (25)  //!< The maximum number of octets added by the MAC sublayer to the PSDU without security
#define ZB_MAC_MAX_MAC_PAYLOAD_SIZE             (ZB_PHY_MAX_PACKET_SIZE - ZB_MAC_MIN_MPDU_OVERHEAD)
                                                      //!< The maximum number of octets that can be transmitted in the MAC Payload field
#define ZB_MAC_MAX_MAC_SAFE_PAYLOAD_SIZE        (ZB_PHY_MAX_PACKET_SIZE - ZB_MAC_MAX_MPDU_UNSECURITY_OVERHEAD)
                                                      //!< The maximum number of octets that can be transmitted in the MAC Payload field of an unsecured MAC frame
#define ZB_MAC_MAX_LOST_BEACONS                 (4)   //!< The number of consecutive lost beacons that will cause the MAC sublayer of a receiving device to declare a loss of synchronization
#define ZB_MAC_MAX_BEACON_OVERHEAD              (75)  //!< The maximum number of octets added by the MAC sublayer to the MAC payload of a beacon frame.
#define ZB_MAC_MAX_BEACON_PAYLOAD_LENGTH        (ZB_PHY_MAX_PACKET_SIZE - ZB_MAC_MAX_BEACON_OVERHEAD)
                                                      //!< The maximum size, in octets, of a beacon payload
#define ZB_MAC_GTS_DESC_PERSISTENCE_TIME        (4)   //!< The number of consecutive lost beacons that will cause the MAC sublayer of a receiving device to declare a loss of synchronization.

/** @} end of group zb_mac_constant */


/* re-transmit timers of the MAC layer */
#define ZB_MAC_WAIT_RESP_TIME_DEFAULT           (32)
#define	ZB_MAC_FRAME_RETRIES_MAX                (3)


#ifdef ZB_MAC_SECURITY
typedef struct zb_mac_device_table_s {
    uint32_t frameCounter;
    uint16_t shortAddr;
    addrExt_t extAddr;
    uint16_t panId;
} zb_mac_device_table_t;
#endif

/** @addtogroup zigbee_mac_pib_type Zigbee Mac PIB Types
 *  @{
 */
typedef struct {
    uint32_t beaconTxTime;                   //!< The time that the device transmitted its last beacon frame
    addrExt_t coordExtAddress;          //!< coordinator ieee address
    addrExt_t extAddress;               //!< ieee address
    uint16_t ackWaitDuration;                //!< the duration for waiting for an acknowledgment frame
    uint16_t coordShortAddress;              //!< coordinator short address
    uint16_t panId;                          //!< pan identifier
    uint16_t shortAddress;                   //!< short address(joined in a network)
    uint16_t superframeOrder;                //!< super-frame order
    uint16_t transactionPersistenceTime;     //! The maximum time (in unit periods) that a transaction is stored by a coordinator and indicated in its beacon.
    uint16_t frameTotalWaitTime;
    uint16_t syncSymbolOffset;//4
    zb_mac_beacon_payload_t beaconPayload;//15
    uint8_t beaconPayloadLen;
    uint8_t associationPermit;
    uint8_t associatedPanCoord;
    uint8_t autoReq;//4
    uint8_t battLifeExt;
    uint8_t battLifeExtPeriods;
    uint8_t beaconOrder;
    uint8_t beaconSeqNum;
    uint8_t seqNum;
    uint8_t frameRetryNum;
    uint8_t respWaitTime;
    uint8_t maxCsmaBackoffs;
    uint8_t promiscuousMode;
    uint8_t timeStampSupported;
    uint8_t securityEnabled;

    uint8_t gtsPermit;
    uint8_t minBe;
    uint8_t maxBe;
    uint8_t phyPageCur;
    uint8_t phyChannelCur;
    uint8_t rxOnWhenIdle;
    uint8_t txPower;
    uint8_t resv[3];//76-bytes

#ifdef ZB_MAC_SECURITY
    zb_mac_device_table_t devTable[MAC_DEVICE_TABLE_SIZE];
    uint8_t devTableEntry;
    uint32_t frameCounter;
    uint8_t secKey[16];
#endif
} zb_mac_pib_t;
/** @} end of group zigbee_mac_pib_type */

#define MAC_IB()        g_zbMacPib

extern zb_mac_pib_t g_zbMacPib;



void generateIEEEAddr(void);

/*
 * MLME-SET.request to set MAC pib attribute from upper layer to MAC layer
 *
 * @param attribute MAC PIB attribute MAC PIB attribute id(Table 86 ?a MAC PIB attributes)
 *
 * @param value the pointer value of the attribute
 *
 * @param len the length of the attribute value
 *
 * @return MAC_SUCCESS if it's successful, or MAC_INVALID_PARAMETER
 *
 * */
uint8_t zb_MacAttrSet(uint8_t attribute, uint8_t *value, uint8_t len);

/*
* MLME-GET.request to get MAC pib attribute from upper layer to MAC layer
*
* @param attribute MAC PIB attribute MAC PIB attribute id(Table 86 ?a MAC PIB attributes)
*
* @param value the pointer to the value of the attribute
*
* @param len the length of the attribute value
*
* @return MAC_SUCCESS if it's successful, or MAC_INVALID_PARAMETER
*
* */
uint8_t zb_MacAttrGet(uint8_t attribute, uint8_t *value, uint8_t *len);

#endif /* ZB_MAC_PIB_H */
