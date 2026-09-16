/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * AES-128 block cipher for the Zigbee security service.
 *
 * Boards whose SoC carries an AES engine reach it through the Zephyr crypto
 * API; everything else, native_sim included, uses the software cipher below.
 */

#include <string.h>

#include <zephyr/zigbee/zb_types.h>

#include "zb_aes.h"

#if defined(CONFIG_CRYPTO_TLSR8258)

#include <zephyr/crypto/crypto.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zigbee_aes, CONFIG_ZIGBEE_LOG_LEVEL);

#define AES_BLOCK_SIZE 16U

/*
 * One long-lived session per direction. A session only caches the key, so
 * reopening it costs a validation and a 16-byte copy; that happens on every
 * AES-MMO block, because Matyas-Meyer-Oseas chains the previous hash state as
 * the next round's key. CCM* keeps one key for a whole frame and reuses the
 * session as is.
 *
 * Only the Zigbee thread calls in here, so the contexts need no locking of
 * their own; the driver serialises access to the engine.
 */
struct zb_aes_session {
	struct cipher_ctx ctx;
	u8 key[AES_BLOCK_SIZE];
	bool open;
};

static struct zb_aes_session zb_aes_enc;
static struct zb_aes_session zb_aes_dec;

static int zb_aes_session_get(struct zb_aes_session *session, enum cipher_op op, const u8 *key)
{
	const struct device *dev = DEVICE_DT_GET_ONE(telink_tlsr8258_aes);
	int ret;

	if (session->open && (memcmp(session->key, key, AES_BLOCK_SIZE) == 0)) {
		return 0;
	}

	if (!device_is_ready(dev)) {
		LOG_ERR("AES device not ready");
		return -ENODEV;
	}

	if (session->open) {
		(void)cipher_free_session(dev, &session->ctx);
		session->open = false;
	}

	memcpy(session->key, key, AES_BLOCK_SIZE);

	session->ctx.keylen = AES_BLOCK_SIZE;
	session->ctx.key.bit_stream = session->key;
	session->ctx.flags = CAP_RAW_KEY | CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS;

	ret = cipher_begin_session(dev, &session->ctx, CRYPTO_CIPHER_ALGO_AES,
				   CRYPTO_CIPHER_MODE_ECB, op);
	if (ret != 0) {
		LOG_ERR("cipher_begin_session failed (%d)", ret);
		return ret;
	}

	session->open = true;

	return 0;
}

static void zb_aes_run(struct zb_aes_session *session, enum cipher_op op, u8 *key, u8 *in, u8 *out)
{
	struct cipher_pkt pkt = {
		.in_buf = in,
		.in_len = (int)AES_BLOCK_SIZE,
		.out_buf = out,
		.out_buf_max = (int)AES_BLOCK_SIZE,
	};
	int ret;

	ret = zb_aes_session_get(session, op, key);
	if (ret == 0) {
		ret = cipher_block_op(&session->ctx, &pkt);
	}

	if (ret != 0) {
		/*
		 * The callers come from the vendor security service and take no
		 * status. Wipe the output so a broken engine shows up as a MIC
		 * mismatch instead of silently reusing whatever was there.
		 */
		LOG_ERR("AES block failed (%d)", ret);
		memset(out, 0, AES_BLOCK_SIZE);
	}
}

void zb_aes_encrypt(u8 *key, u8 *plain, u8 *result)
{
	zb_aes_run(&zb_aes_enc, CRYPTO_CIPHER_OP_ENCRYPT, key, plain, result);
}

void zb_aes_decrypt(u8 *key, u8 *cipher, u8 *result)
{
	zb_aes_run(&zb_aes_dec, CRYPTO_CIPHER_OP_DECRYPT, key, cipher, result);
}

#else /* CONFIG_CRYPTO_TLSR8258 */

#define AES_BLOCK_SIZE 16U
#define AES_ROUND_KEYS 176U

static const u8 aes_sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7,
	0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf,
	0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5,
	0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e,
	0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
	0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef,
	0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
	0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d,
	0x64, 0x5d, 0x19, 0x73, 0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee,
	0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5,
	0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e,
	0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e,
	0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55,
	0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
	0xb0, 0x54, 0xbb, 0x16
};

static const u8 aes_inv_sbox[256] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3,
	0xd7, 0xfb, 0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44,
	0xc4, 0xde, 0xe9, 0xcb, 0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c,
	0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e, 0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25, 0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68,
	0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92, 0x6c, 0x70, 0x48, 0x50,
	0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84, 0x90, 0xd8,
	0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13,
	0x8a, 0x6b, 0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce,
	0xf0, 0xb4, 0xe6, 0x73, 0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9,
	0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e, 0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b, 0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2,
	0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4, 0x1f, 0xdd, 0xa8, 0x33,
	0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f, 0x60, 0x51,
	0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53,
	0x99, 0x61, 0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63,
	0x55, 0x21, 0x0c, 0x7d
};

static const u8 aes_rcon[11] = {
	0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static u8 aes_xtime(u8 v)
{
	return (u8)((v << 1) ^ (((v >> 7) & 0x01U) * 0x1bU));
}

static u8 aes_mul(u8 a, u8 b)
{
	u8 result = 0U;
	u8 value = a;
	u8 factor = b;

	while (factor != 0U) {
		if ((factor & 0x01U) != 0U) {
			result ^= value;
		}
		value = aes_xtime(value);
		factor >>= 1;
	}

	return result;
}

static void aes_add_round_key(u8 state[16], const u8 *round_key)
{
	for (u8 i = 0U; i < AES_BLOCK_SIZE; i++) {
		state[i] ^= round_key[i];
	}
}

static void aes_sub_bytes(u8 state[16])
{
	for (u8 i = 0U; i < AES_BLOCK_SIZE; i++) {
		state[i] = aes_sbox[state[i]];
	}
}

static void aes_inv_sub_bytes(u8 state[16])
{
	for (u8 i = 0U; i < AES_BLOCK_SIZE; i++) {
		state[i] = aes_inv_sbox[state[i]];
	}
}

static void aes_shift_rows(u8 state[16])
{
	u8 tmp;

	tmp = state[1];
	state[1] = state[5];
	state[5] = state[9];
	state[9] = state[13];
	state[13] = tmp;

	tmp = state[2];
	state[2] = state[10];
	state[10] = tmp;
	tmp = state[6];
	state[6] = state[14];
	state[14] = tmp;

	tmp = state[3];
	state[3] = state[15];
	state[15] = state[11];
	state[11] = state[7];
	state[7] = tmp;
}

static void aes_inv_shift_rows(u8 state[16])
{
	u8 tmp;

	tmp = state[13];
	state[13] = state[9];
	state[9] = state[5];
	state[5] = state[1];
	state[1] = tmp;

	tmp = state[2];
	state[2] = state[10];
	state[10] = tmp;
	tmp = state[6];
	state[6] = state[14];
	state[14] = tmp;

	tmp = state[3];
	state[3] = state[7];
	state[7] = state[11];
	state[11] = state[15];
	state[15] = tmp;
}

static void aes_mix_columns(u8 state[16])
{
	for (u8 col = 0U; col < 4U; col++) {
		u8 *c = &state[col * 4U];
		u8 s0 = c[0];
		u8 s1 = c[1];
		u8 s2 = c[2];
		u8 s3 = c[3];

		c[0] = aes_mul(s0, 2U) ^ aes_mul(s1, 3U) ^ s2 ^ s3;
		c[1] = s0 ^ aes_mul(s1, 2U) ^ aes_mul(s2, 3U) ^ s3;
		c[2] = s0 ^ s1 ^ aes_mul(s2, 2U) ^ aes_mul(s3, 3U);
		c[3] = aes_mul(s0, 3U) ^ s1 ^ s2 ^ aes_mul(s3, 2U);
	}
}

static void aes_inv_mix_columns(u8 state[16])
{
	for (u8 col = 0U; col < 4U; col++) {
		u8 *c = &state[col * 4U];
		u8 s0 = c[0];
		u8 s1 = c[1];
		u8 s2 = c[2];
		u8 s3 = c[3];

		c[0] = aes_mul(s0, 14U) ^ aes_mul(s1, 11U) ^ aes_mul(s2, 13U) ^ aes_mul(s3, 9U);
		c[1] = aes_mul(s0, 9U) ^ aes_mul(s1, 14U) ^ aes_mul(s2, 11U) ^ aes_mul(s3, 13U);
		c[2] = aes_mul(s0, 13U) ^ aes_mul(s1, 9U) ^ aes_mul(s2, 14U) ^ aes_mul(s3, 11U);
		c[3] = aes_mul(s0, 11U) ^ aes_mul(s1, 13U) ^ aes_mul(s2, 9U) ^ aes_mul(s3, 14U);
	}
}

static void aes_key_expand(const u8 key[16], u8 round_keys[AES_ROUND_KEYS])
{
	u8 bytes_generated = AES_BLOCK_SIZE;
	u8 rcon_index = 1U;
	u8 temp[4];

	memcpy(round_keys, key, AES_BLOCK_SIZE);

	while (bytes_generated < AES_ROUND_KEYS) {
		for (u8 i = 0U; i < 4U; i++) {
			temp[i] = round_keys[bytes_generated - 4U + i];
		}

		if ((bytes_generated % AES_BLOCK_SIZE) == 0U) {
			u8 swap = temp[0];

			temp[0] = aes_sbox[temp[1]] ^ aes_rcon[rcon_index++];
			temp[1] = aes_sbox[temp[2]];
			temp[2] = aes_sbox[temp[3]];
			temp[3] = aes_sbox[swap];
		}

		for (u8 i = 0U; i < 4U; i++) {
			round_keys[bytes_generated] = round_keys[bytes_generated - AES_BLOCK_SIZE] ^ temp[i];
			bytes_generated++;
		}
	}
}

static void aes_encrypt_block_sw(const u8 key[16], const u8 in[16], u8 out[16])
{
	u8 state[16];
	u8 round_keys[AES_ROUND_KEYS];

	memcpy(state, in, sizeof(state));
	aes_key_expand(key, round_keys);

	aes_add_round_key(state, round_keys);
	for (u8 round = 1U; round < 10U; round++) {
		aes_sub_bytes(state);
		aes_shift_rows(state);
		aes_mix_columns(state);
		aes_add_round_key(state, &round_keys[round * AES_BLOCK_SIZE]);
	}

	aes_sub_bytes(state);
	aes_shift_rows(state);
	aes_add_round_key(state, &round_keys[10U * AES_BLOCK_SIZE]);
	memcpy(out, state, sizeof(state));
}

static void aes_decrypt_block_sw(const u8 key[16], const u8 in[16], u8 out[16])
{
	u8 state[16];
	u8 round_keys[AES_ROUND_KEYS];

	memcpy(state, in, sizeof(state));
	aes_key_expand(key, round_keys);

	aes_add_round_key(state, &round_keys[10U * AES_BLOCK_SIZE]);
	for (int round = 9; round > 0; round--) {
		aes_inv_shift_rows(state);
		aes_inv_sub_bytes(state);
		aes_add_round_key(state, &round_keys[round * AES_BLOCK_SIZE]);
		aes_inv_mix_columns(state);
	}

	aes_inv_shift_rows(state);
	aes_inv_sub_bytes(state);
	aes_add_round_key(state, round_keys);
	memcpy(out, state, sizeof(state));
}

void zb_aes_encrypt(u8 *key, u8 *plain, u8 *result)
{
	aes_encrypt_block_sw(key, plain, result);
}

void zb_aes_decrypt(u8 *key, u8 *cipher, u8 *result)
{
	aes_decrypt_block_sw(key, cipher, result);
}

#endif /* CONFIG_CRYPTO_TLSR8258 */
