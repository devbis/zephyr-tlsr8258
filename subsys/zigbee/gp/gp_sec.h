/********************************************************************************************************
 * @file    gp_sec.h
 *
 * @brief   This is the header file for gp_sec
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
#ifndef GP_SEC_H
#define GP_SEC_H


uint8_t gpCcmStar(uint8_t appId, gpdId_t gpdId,
             uint8_t gpdfSecKey, uint8_t gpdfSecLevel,
             uint8_t endpoint, uint32_t secFrameCounter,
             uint8_t gpdAsduLen, uint8_t *gpdAsdu,
             bool autoComm, bool rxAfterTx,
             uint32_t mic, uint8_t *pKey);

uint8_t gpdKeyWithTCLK(uint8_t appId, gpdId_t gpdId,
                  uint8_t dataLen, uint8_t *pData,
                  uint32_t frameCounter, uint8_t *mic,
                  uint8_t *pKey, bool enOrDecrypt);

#endif	/* GP_SEC_H */
