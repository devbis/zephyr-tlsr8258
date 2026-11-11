/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/sys/util.h>

#include "zb_aes.h"
#include "zb_minimal_ccm.h"

#define ZB_MINIMAL_CCM_L_VAL 2U

static void zb_minimal_aes_encrypt_block(const uint8_t *key, const uint8_t *in, uint8_t *out)
{
	zb_aes_encrypt((uint8_t *)key, (uint8_t *)in, out);
}

static void zb_minimal_ccm_xor_block(uint8_t *dst, const uint8_t *src)
{
	for (uint8_t i = 0U; i < 16U; i++) {
		dst[i] ^= src[i];
	}
}

static void zb_minimal_ccm_mac_block(const uint8_t *key, uint8_t state[16], const uint8_t block[16])
{
	uint8_t tmp[16];

	memcpy(tmp, state, sizeof(tmp));
	zb_minimal_ccm_xor_block(tmp, block);
	zb_minimal_aes_encrypt_block(key, tmp, state);
}

static void zb_minimal_ccm_ctr_block(const uint8_t *key, const uint8_t nonce[13], uint16_t counter, uint8_t stream[16])
{
	uint8_t a_i[16] = {0};

	a_i[0] = ZB_MINIMAL_CCM_L_VAL - 1U;
	memcpy(&a_i[1], nonce, 13U);
	a_i[14] = (uint8_t)(counter >> 8);
	a_i[15] = (uint8_t)counter;
	zb_minimal_aes_encrypt_block(key, a_i, stream);
}

static void zb_minimal_ccm_add_aad(const uint8_t *key, uint8_t state[16], const uint8_t *aad, uint8_t aad_len)
{
	uint8_t block[16] = {0};
	uint8_t idx = 0U;
	uint8_t off = 0U;

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

static void zb_minimal_ccm_add_msg(const uint8_t *key, uint8_t state[16], const uint8_t *msg, uint8_t msg_len)
{
	uint8_t block[16] = {0};
	uint8_t off = 0U;
	uint8_t idx;

	while (off < msg_len) {
		memset(block, 0, sizeof(block));
		for (idx = 0U; (idx < sizeof(block)) && (off < msg_len); idx++) {
			block[idx] = msg[off++];
		}
		zb_minimal_ccm_mac_block(key, state, block);
	}
}

uint8_t zb_minimal_ccm_encrypt_auth(const uint8_t *key, const uint8_t nonce[13], uint8_t mic_len, const uint8_t *aad,
			       uint8_t aad_len, uint8_t *msg, uint8_t msg_len, uint8_t *mic_out)
{
	uint8_t mac_state[16] = {0};
	uint8_t b0[16] = {0};
	uint8_t s0[16];
	uint8_t stream[16];
	uint8_t flags = (uint8_t)(ZB_MINIMAL_CCM_L_VAL - 1U);
	uint8_t off = 0U;
	uint16_t counter = 1U;

	if ((key == NULL) || (nonce == NULL) || (msg == NULL) || (mic_out == NULL) ||
	    (mic_len != 4U)) {
		return 0U;
	}

	if (aad_len != 0U) {
		flags |= 0x40U;
	}
	flags |= (uint8_t)(((mic_len - 2U) / 2U) << 3);
	b0[0] = flags;
	memcpy(&b0[1], nonce, 13U);
	b0[14] = (uint8_t)(msg_len >> 8);
	b0[15] = msg_len;

	zb_minimal_ccm_mac_block(key, mac_state, b0);
	zb_minimal_ccm_add_aad(key, mac_state, aad, aad_len);
	zb_minimal_ccm_add_msg(key, mac_state, msg, msg_len);

	zb_minimal_ccm_ctr_block(key, nonce, 0U, s0);

	while (off < msg_len) {
		uint8_t blk_len = MIN((uint8_t)16U, (uint8_t)(msg_len - off));

		zb_minimal_ccm_ctr_block(key, nonce, counter++, stream);
		for (uint8_t i = 0U; i < blk_len; i++) {
			msg[off + i] ^= stream[i];
		}
		off += blk_len;
	}

	/*
	 * Match the vendor aes_ccmAuthTran()+aes_ccmEncTran() ordering:
	 * finish in-place payload encryption before publishing the MIC.  The
	 * radio DMA can observe a carrier while the stack is preparing it; writing
	 * MIC first exposed a transient plaintext+valid-MIC frame on air.
	 */
	for (uint8_t i = 0U; i < mic_len; i++) {
		mic_out[i] = mac_state[i] ^ s0[i];
	}

	return (uint8_t)(msg_len + mic_len);
}

bool zb_minimal_ccm_decrypt_auth(const uint8_t *key, const uint8_t nonce[13], uint8_t mic_len, uint8_t *cipher,
				 uint8_t cipher_len, const uint8_t *aad, uint8_t aad_len, const uint8_t *mic)
{
	uint8_t mac_state[16] = {0};
	uint8_t b0[16] = {0};
	uint8_t s0[16];
	uint8_t tag[16];
	uint8_t stream[16];
	uint8_t flags = (uint8_t)(ZB_MINIMAL_CCM_L_VAL - 1U);
	uint8_t off = 0U;
	uint16_t counter = 1U;

	if ((key == NULL) || (nonce == NULL) || (cipher == NULL) || (mic == NULL) ||
	    (mic_len != 4U)) {
		return false;
	}

	while (off < cipher_len) {
		uint8_t blk_len = MIN((uint8_t)16U, (uint8_t)(cipher_len - off));

		zb_minimal_ccm_ctr_block(key, nonce, counter++, stream);
		for (uint8_t i = 0U; i < blk_len; i++) {
			cipher[off + i] ^= stream[i];
		}
		off += blk_len;
	}

	if (aad_len != 0U) {
		flags |= 0x40U;
	}
	flags |= (uint8_t)(((mic_len - 2U) / 2U) << 3);
	b0[0] = flags;
	memcpy(&b0[1], nonce, 13U);
	b0[14] = (uint8_t)(cipher_len >> 8);
	b0[15] = cipher_len;

	zb_minimal_ccm_mac_block(key, mac_state, b0);
	zb_minimal_ccm_add_aad(key, mac_state, aad, aad_len);
	zb_minimal_ccm_add_msg(key, mac_state, cipher, cipher_len);

	zb_minimal_ccm_ctr_block(key, nonce, 0U, s0);
	for (uint8_t i = 0U; i < mic_len; i++) {
		tag[i] = mac_state[i] ^ s0[i];
	}

	return memcmp(tag, mic, mic_len) == 0;
}
