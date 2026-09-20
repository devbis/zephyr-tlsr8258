/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ZB_MINIMAL_CCM_H
#define ZB_MINIMAL_CCM_H

#include <zephyr/zigbee/zb_types.h>

u8 zb_minimal_ccm_encrypt_auth(const u8 *key, const u8 nonce[13], u8 mic_len,
			       const u8 *aad, u8 aad_len, u8 *msg, u8 msg_len, u8 *mic_out);

bool zb_minimal_ccm_decrypt_auth(const u8 *key, const u8 nonce[13], u8 mic_len, u8 *cipher,
				 u8 cipher_len, const u8 *aad, u8 aad_len, const u8 *mic);

#endif
