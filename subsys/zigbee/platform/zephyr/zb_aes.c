/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */

/*
 * AES-128 block cipher for the Zigbee security service.
 *
 * Boards whose SoC carries an AES engine reach it through the Zephyr crypto
 * API; everything else, native_sim included, uses the software cipher in
 * zb_aes_sw.c. Supported engines: Telink TLSR8258, Espressif and Nordic nRF
 * ECB. The nRF ECB peripheral only encrypts, so its decrypt direction (used
 * by OTA and touchlink only) runs in software.
 */

#include <string.h>

#include <zephyr/zigbee/zb_types.h>

#include "zb_aes.h"

#if defined(CONFIG_CRYPTO_TLSR8258)
#define ZB_AES_DEVICE()      DEVICE_DT_GET_ONE(telink_tlsr8258_aes)
#define ZB_AES_HW_DECRYPT    1
#elif defined(CONFIG_CRYPTO_ESP32_AES)
#define ZB_AES_DEVICE()      DEVICE_DT_GET_ONE(espressif_esp32_aes)
#define ZB_AES_HW_DECRYPT    1
#elif defined(CONFIG_CRYPTO_NRF_ECB)
#define ZB_AES_DEVICE()      DEVICE_DT_GET_ONE(nordic_nrf_ecb)
#endif

#if defined(ZB_AES_DEVICE)

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
#if defined(ZB_AES_HW_DECRYPT)
static struct zb_aes_session zb_aes_dec;
#endif

static int zb_aes_session_get(struct zb_aes_session *session, enum cipher_op op, const u8 *key)
{
	const struct device *dev = ZB_AES_DEVICE();
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

#endif /* ZB_AES_DEVICE */

void zb_aes_encrypt(u8 *key, u8 *plain, u8 *result)
{
#if defined(ZB_AES_DEVICE)
	zb_aes_run(&zb_aes_enc, CRYPTO_CIPHER_OP_ENCRYPT, key, plain, result);
#else
	zb_aes_sw_encrypt(key, plain, result);
#endif
}

void zb_aes_decrypt(u8 *key, u8 *cipher, u8 *result)
{
#if defined(ZB_AES_HW_DECRYPT)
	zb_aes_run(&zb_aes_dec, CRYPTO_CIPHER_OP_DECRYPT, key, cipher, result);
#else
	zb_aes_sw_decrypt(key, cipher, result);
#endif
}
