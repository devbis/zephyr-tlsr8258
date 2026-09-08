/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Self-contained AES-128 + CCM* for the host coordinator daemon. The AES core
 * is a compact textbook implementation (encrypt only); the CCM* layer is a
 * straight port of subsys/zigbee/common/zb_minimal_ccm.c (L=2, 4-byte MIC) so
 * it produces byte-identical output to the device's NWK security.
 */
#include <string.h>

#include "coord_ccm.h"

/* ---- AES-128 (encrypt only) ------------------------------------------- */

static const uint8_t aes_sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static uint8_t xtime(uint8_t x)
{
	return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1bU));
}

static void aes_key_expansion(const uint8_t key[16], uint8_t rk[176])
{
	static const uint8_t rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
					 0x20, 0x40, 0x80, 0x1b, 0x36};
	uint8_t t[4];

	memcpy(rk, key, 16);
	for (int i = 16, r = 0; i < 176; i += 4) {
		memcpy(t, &rk[i - 4], 4);
		if ((i % 16) == 0) {
			uint8_t tmp = t[0];

			t[0] = (uint8_t)(aes_sbox[t[1]] ^ rcon[r++]);
			t[1] = aes_sbox[t[2]];
			t[2] = aes_sbox[t[3]];
			t[3] = aes_sbox[tmp];
		}
		for (int j = 0; j < 4; j++) {
			rk[i + j] = (uint8_t)(rk[i - 16 + j] ^ t[j]);
		}
	}
}

void coord_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
	uint8_t rk[176];
	uint8_t s[16];

	aes_key_expansion(key, rk);
	memcpy(s, in, 16);

	for (int i = 0; i < 16; i++) {
		s[i] ^= rk[i];
	}

	for (int round = 1; round <= 10; round++) {
		uint8_t tmp[16];

		/* SubBytes */
		for (int i = 0; i < 16; i++) {
			s[i] = aes_sbox[s[i]];
		}
		/* ShiftRows (column-major state: byte index = col*4 + row) */
		memcpy(tmp, s, 16);
		for (int row = 1; row < 4; row++) {
			for (int col = 0; col < 4; col++) {
				s[col * 4 + row] = tmp[((col + row) % 4) * 4 + row];
			}
		}
		/* MixColumns (skip on final round) */
		if (round != 10) {
			for (int col = 0; col < 4; col++) {
				uint8_t *c = &s[col * 4];
				uint8_t a0 = c[0], a1 = c[1], a2 = c[2], a3 = c[3];

				c[0] = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
				c[1] = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
				c[2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
				c[3] = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
			}
		}
		/* AddRoundKey */
		for (int i = 0; i < 16; i++) {
			s[i] ^= rk[round * 16 + i];
		}
	}

	memcpy(out, s, 16);
}

/* ---- CCM* (port of zb_minimal_ccm.c, L=2, 4-byte MIC) ----------------- */

#define CCM_L_VAL 2U

static void ccm_xor_block(uint8_t *dst, const uint8_t *src)
{
	for (uint8_t i = 0U; i < 16U; i++) {
		dst[i] ^= src[i];
	}
}

static void ccm_mac_block(const uint8_t *key, uint8_t state[16], const uint8_t block[16])
{
	uint8_t tmp[16];

	memcpy(tmp, state, sizeof(tmp));
	ccm_xor_block(tmp, block);
	coord_aes128_encrypt(key, tmp, state);
}

static void ccm_ctr_block(const uint8_t *key, const uint8_t nonce[13], uint16_t counter,
			  uint8_t stream[16])
{
	uint8_t a_i[16] = {0};

	a_i[0] = CCM_L_VAL - 1U;
	memcpy(&a_i[1], nonce, 13U);
	a_i[14] = (uint8_t)(counter >> 8);
	a_i[15] = (uint8_t)counter;
	coord_aes128_encrypt(key, a_i, stream);
}

static void ccm_add_aad(const uint8_t *key, uint8_t state[16], const uint8_t *aad, uint8_t aad_len)
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
	ccm_mac_block(key, state, block);

	while (off < aad_len) {
		memset(block, 0, sizeof(block));
		for (idx = 0U; (idx < sizeof(block)) && (off < aad_len); idx++) {
			block[idx] = aad[off++];
		}
		ccm_mac_block(key, state, block);
	}
}

static void ccm_add_msg(const uint8_t *key, uint8_t state[16], const uint8_t *msg, uint8_t msg_len)
{
	uint8_t block[16] = {0};
	uint8_t off = 0U;
	uint8_t idx;

	while (off < msg_len) {
		memset(block, 0, sizeof(block));
		for (idx = 0U; (idx < sizeof(block)) && (off < msg_len); idx++) {
			block[idx] = msg[off++];
		}
		ccm_mac_block(key, state, block);
	}
}

static void ccm_tag(const uint8_t *key, const uint8_t nonce[13], const uint8_t *aad,
		    uint8_t aad_len, const uint8_t *msg, uint8_t msg_len, uint8_t tag[4])
{
	uint8_t mac_state[16] = {0};
	uint8_t b0[16] = {0};
	uint8_t s0[16];
	uint8_t flags = (uint8_t)(CCM_L_VAL - 1U);

	if (aad_len != 0U) {
		flags |= 0x40U;
	}
	flags |= (uint8_t)(((4U - 2U) / 2U) << 3); /* M=4 */
	b0[0] = flags;
	memcpy(&b0[1], nonce, 13U);
	b0[14] = (uint8_t)(msg_len >> 8);
	b0[15] = msg_len;

	ccm_mac_block(key, mac_state, b0);
	ccm_add_aad(key, mac_state, aad, aad_len);
	ccm_add_msg(key, mac_state, msg, msg_len);

	ccm_ctr_block(key, nonce, 0U, s0);
	for (uint8_t i = 0U; i < 4U; i++) {
		tag[i] = mac_state[i] ^ s0[i];
	}
}

static void ccm_crypt(const uint8_t *key, const uint8_t nonce[13], uint8_t *msg, uint8_t msg_len)
{
	uint8_t off = 0U;
	uint16_t counter = 1U;
	uint8_t stream[16];

	while (off < msg_len) {
		uint8_t blk = (uint8_t)((msg_len - off) < 16U ? (msg_len - off) : 16U);

		ccm_ctr_block(key, nonce, counter++, stream);
		for (uint8_t i = 0U; i < blk; i++) {
			msg[off + i] ^= stream[i];
		}
		off += blk;
	}
}

uint8_t coord_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13],
			  const uint8_t *aad, uint8_t aad_len,
			  uint8_t *msg, uint8_t msg_len, uint8_t mic_out[4])
{
	if ((key == NULL) || (nonce == NULL) || (mic_out == NULL)) {
		return 0U;
	}

	/* MIC is computed over the plaintext, then the plaintext is encrypted. */
	ccm_tag(key, nonce, aad, aad_len, msg, msg_len, mic_out);
	ccm_crypt(key, nonce, msg, msg_len);
	return (uint8_t)(msg_len + 4U);
}

bool coord_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13],
		       uint8_t *cipher, uint8_t cipher_len,
		       const uint8_t *aad, uint8_t aad_len, const uint8_t mic[4])
{
	uint8_t tag[4];

	if ((key == NULL) || (nonce == NULL) || (mic == NULL)) {
		return false;
	}

	/* Decrypt in place, then recompute the MIC over the recovered plaintext. */
	ccm_crypt(key, nonce, cipher, cipher_len);
	ccm_tag(key, nonce, aad, aad_len, cipher, cipher_len, tag);
	return memcmp(tag, mic, 4) == 0;
}
