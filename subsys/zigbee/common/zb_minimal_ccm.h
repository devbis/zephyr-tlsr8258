/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZB_MINIMAL_CCM_H
#define ZB_MINIMAL_CCM_H

#include <zephyr/zigbee/zb_types.h>

uint8_t zb_minimal_ccm_encrypt_auth(const uint8_t *key, const uint8_t nonce[13], uint8_t mic_len,
			       const uint8_t *aad, uint8_t aad_len, uint8_t *msg, uint8_t msg_len, uint8_t *mic_out);

bool zb_minimal_ccm_decrypt_auth(const uint8_t *key, const uint8_t nonce[13], uint8_t mic_len, uint8_t *cipher,
				 uint8_t cipher_len, const uint8_t *aad, uint8_t aad_len, const uint8_t *mic);

#endif
