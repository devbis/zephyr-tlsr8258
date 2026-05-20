/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/sys/util.h>

#include "drv_security.h"
#include "zb_minimal_ccm.h"

#define ZB_MINIMAL_CCM_L_VAL 2U

static void zb_minimal_aes_encrypt_block(const u8 *key, const u8 *in, u8 *out)
{
	drv_aes_encrypt((u8 *)key, (u8 *)in, out);
}

static void zb_minimal_ccm_xor_block(u8 *dst, const u8 *src)
{
	for (u8 i = 0U; i < 16U; i++) {
		dst[i] ^= src[i];
	}
}

static void zb_minimal_ccm_mac_block(const u8 *key, u8 state[16], const u8 block[16])
{
	u8 tmp[16];

	memcpy(tmp, state, sizeof(tmp));
	zb_minimal_ccm_xor_block(tmp, block);
	zb_minimal_aes_encrypt_block(key, tmp, state);
}

static void zb_minimal_ccm_ctr_block(const u8 *key, const u8 nonce[13], u16 counter, u8 stream[16])
{
	u8 a_i[16] = {0};

	a_i[0] = ZB_MINIMAL_CCM_L_VAL - 1U;
	memcpy(&a_i[1], nonce, 13U);
	a_i[14] = (u8)(counter >> 8);
	a_i[15] = (u8)counter;
	zb_minimal_aes_encrypt_block(key, a_i, stream);
}

static void zb_minimal_ccm_add_aad(const u8 *key, u8 state[16], const u8 *aad, u8 aad_len)
{
	u8 block[16] = {0};
	u8 idx = 0U;
	u8 off = 0U;

	if ((aad == NULL) || (aad_len == 0U)) {
		return;
	}

	block[idx++] = 0U;
	block[idx++] = aad_len;
	while ((off < aad_len) && (idx < sizeof(block))) {
		block[idx++] = aad[off++];
	}
	zb_minimal_ccm_mac_block(key, state, block);

	while (off < aad_len) {
		memset(block, 0, sizeof(block));
		for (idx = 0U; (idx < sizeof(block)) && (off < aad_len); idx++) {
			block[idx] = aad[off++];
		}
		zb_minimal_ccm_mac_block(key, state, block);
	}
}

static void zb_minimal_ccm_add_msg(const u8 *key, u8 state[16], const u8 *msg, u8 msg_len)
{
	u8 block[16] = {0};
	u8 off = 0U;
	u8 idx;

	while (off < msg_len) {
		memset(block, 0, sizeof(block));
		for (idx = 0U; (idx < sizeof(block)) && (off < msg_len); idx++) {
			block[idx] = msg[off++];
		}
		zb_minimal_ccm_mac_block(key, state, block);
	}
}

u8 zb_minimal_ccm_encrypt_auth(const u8 *key, const u8 nonce[13], u8 mic_len,
			       const u8 *aad, u8 aad_len, u8 *msg, u8 msg_len, u8 *mic_out)
{
	u8 mac_state[16] = {0};
	u8 b0[16] = {0};
	u8 s0[16];
	u8 stream[16];
	u8 flags = (u8)(ZB_MINIMAL_CCM_L_VAL - 1U);
	u8 off = 0U;
	u16 counter = 1U;

	if ((key == NULL) || (nonce == NULL) || (msg == NULL) || (mic_out == NULL) ||
	    (mic_len != 4U)) {
		return 0U;
	}

	if (aad_len != 0U) {
		flags |= 0x40U;
	}
	flags |= (u8)(((mic_len - 2U) / 2U) << 3);
	b0[0] = flags;
	memcpy(&b0[1], nonce, 13U);
	b0[14] = (u8)(msg_len >> 8);
	b0[15] = msg_len;

	zb_minimal_ccm_mac_block(key, mac_state, b0);
	zb_minimal_ccm_add_aad(key, mac_state, aad, aad_len);
	zb_minimal_ccm_add_msg(key, mac_state, msg, msg_len);

	zb_minimal_ccm_ctr_block(key, nonce, 0U, s0);
	for (u8 i = 0U; i < mic_len; i++) {
		mic_out[i] = mac_state[i] ^ s0[i];
	}

	while (off < msg_len) {
		u8 blk_len = MIN((u8)16U, (u8)(msg_len - off));

		zb_minimal_ccm_ctr_block(key, nonce, counter++, stream);
		for (u8 i = 0U; i < blk_len; i++) {
			msg[off + i] ^= stream[i];
		}
		off += blk_len;
	}

	return (u8)(msg_len + mic_len);
}

bool zb_minimal_ccm_decrypt_auth(const u8 *key, const u8 nonce[13], u8 mic_len, u8 *cipher,
				 u8 cipher_len, const u8 *aad, u8 aad_len, const u8 *mic)
{
	u8 mac_state[16] = {0};
	u8 b0[16] = {0};
	u8 s0[16];
	u8 tag[16];
	u8 stream[16];
	u8 flags = (u8)(ZB_MINIMAL_CCM_L_VAL - 1U);
	u8 off = 0U;
	u16 counter = 1U;

	if ((key == NULL) || (nonce == NULL) || (cipher == NULL) || (mic == NULL) ||
	    (mic_len != 4U)) {
		return false;
	}

	while (off < cipher_len) {
		u8 blk_len = MIN((u8)16U, (u8)(cipher_len - off));

		zb_minimal_ccm_ctr_block(key, nonce, counter++, stream);
		for (u8 i = 0U; i < blk_len; i++) {
			cipher[off + i] ^= stream[i];
		}
		off += blk_len;
	}

	if (aad_len != 0U) {
		flags |= 0x40U;
	}
	flags |= (u8)(((mic_len - 2U) / 2U) << 3);
	b0[0] = flags;
	memcpy(&b0[1], nonce, 13U);
	b0[14] = (u8)(cipher_len >> 8);
	b0[15] = cipher_len;

	zb_minimal_ccm_mac_block(key, mac_state, b0);
	zb_minimal_ccm_add_aad(key, mac_state, aad, aad_len);
	zb_minimal_ccm_add_msg(key, mac_state, cipher, cipher_len);

	zb_minimal_ccm_ctr_block(key, nonce, 0U, s0);
	for (u8 i = 0U; i < mic_len; i++) {
		tag[i] = mac_state[i] ^ s0[i];
	}

	return memcmp(tag, mic, mic_len) == 0;
}
