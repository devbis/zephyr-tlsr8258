/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_AES_H_
#define ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_AES_H_

#include <zephyr/zigbee/zb_types.h>

/**
 * @brief Encrypt one AES-128 block.
 *
 * The security service calls this for CCM* keystream and CBC-MAC blocks and
 * for the AES-MMO hash, which passes the same buffer as @p key and @p result.
 *
 * @param key    16-byte key.
 * @param plain  16-byte plaintext block.
 * @param result 16-byte output block, may alias @p key or @p plain.
 */
void zb_aes_encrypt(u8 *key, u8 *plain, u8 *result);

/**
 * @brief Decrypt one AES-128 block.
 *
 * @param key    16-byte key.
 * @param cipher 16-byte ciphertext block.
 * @param result 16-byte output block, may alias @p key or @p cipher.
 */
void zb_aes_decrypt(u8 *key, u8 *cipher, u8 *result);

#endif /* ZEPHYR_SUBSYS_ZIGBEE_PLATFORM_ZEPHYR_ZB_AES_H_ */
