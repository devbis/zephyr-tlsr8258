/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/crypto/crypto.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <string.h>

#define DT_DRV_COMPAT telink_tlsr8258_aes

LOG_MODULE_REGISTER(crypto_tlsr8258, CONFIG_CRYPTO_LOG_LEVEL);

#define AES_KEY_SIZE   16U
#define AES_BLOCK_SIZE 16U
#define AES_BLOCK_WORDS (AES_BLOCK_SIZE / 4U)

/*
 * Register block, relative to the devicetree base address:
 *
 *   +0x00 ctrl (u8)   bit0 CODEC_TRIG (0 = encrypt, 1 = decrypt)
 *                     bit1 DATA_FEED  (set while the engine wants input)
 *                     bit2 FINISHED   (set once the result can be read)
 *   +0x08 data (u32)  one 32-bit port, written four times to feed a block
 *                     and read four times to drain the result
 *   +0x10 key (u8[16]) key register file
 */
#define AES_OFF_CTRL 0x00U
#define AES_OFF_DATA 0x08U
#define AES_OFF_KEY  0x10U

#define AES_CTRL_DECRYPT BIT(0)
#define AES_CTRL_FEED    BIT(1)
#define AES_CTRL_DONE    BIT(2)

/*
 * The engine completes a block in a few dozen CPU cycles. The bound only has
 * to be large enough to never trip on working hardware, and small enough that
 * a dead engine reports an error instead of hanging the calling thread.
 */
#define AES_SPIN_LIMIT 10000U

#define AES_CAPS (CAP_RAW_KEY | CAP_SEPARATE_IO_BUFS | CAP_SYNC_OPS)

struct crypto_tlsr8258_config {
	uintptr_t base;
};

struct crypto_tlsr8258_session {
	uint8_t key[AES_KEY_SIZE];
	bool decrypt;
	bool in_use;
};

struct crypto_tlsr8258_data {
	struct crypto_tlsr8258_session sessions[CONFIG_CRYPTO_TLSR8258_MAX_SESSION];
	struct k_spinlock lock;
};

static inline volatile uint8_t *aes_ctrl(const struct crypto_tlsr8258_config *cfg)
{
	return (volatile uint8_t *)(cfg->base + AES_OFF_CTRL);
}

static inline volatile uint32_t *aes_data(const struct crypto_tlsr8258_config *cfg)
{
	return (volatile uint32_t *)(cfg->base + AES_OFF_DATA);
}

static inline volatile uint8_t *aes_key(const struct crypto_tlsr8258_config *cfg)
{
	return (volatile uint8_t *)(cfg->base + AES_OFF_KEY);
}

static int crypto_tlsr8258_block(const struct device *dev,
				 const struct crypto_tlsr8258_session *session,
				 const uint8_t *in, uint8_t *out)
{
	const struct crypto_tlsr8258_config *cfg = dev->config;
	struct crypto_tlsr8258_data *data = dev->data;
	volatile uint8_t *ctrl = aes_ctrl(cfg);
	volatile uint32_t *port = aes_data(cfg);
	volatile uint8_t *keyreg = aes_key(cfg);
	/*
	 * AES-MMO chains the previous hash state as the next round's key, so
	 * the Zigbee security service calls in with `key` and `out` pointing
	 * at the same buffer. Staging the key here decouples the key load from
	 * the output write-back: without it the derived Transport-Key encryption
	 * key for "ZigBeeAlliance09" comes back wrong on this engine.
	 */
	uint8_t key_local[AES_KEY_SIZE];
	uint32_t block[AES_BLOCK_WORDS];
	unsigned int fed = 0U;
	unsigned int spins;
	int ret = 0;

	memcpy(key_local, session->key, AES_KEY_SIZE);

	for (unsigned int i = 0U; i < AES_BLOCK_WORDS; i++) {
		const uint8_t *p = &in[i * 4U];

		block[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
			   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	}

	K_SPINLOCK(&data->lock) {
		if (session->decrypt) {
			*ctrl |= AES_CTRL_DECRYPT;
		} else {
			*ctrl &= ~AES_CTRL_DECRYPT;
		}

		for (unsigned int i = 0U; i < AES_KEY_SIZE; i++) {
			keyreg[i] = key_local[i];
		}

		while ((fed < AES_BLOCK_WORDS) && ((*ctrl & AES_CTRL_FEED) != 0U)) {
			*port = block[fed];
			fed++;
		}

		if (fed != AES_BLOCK_WORDS) {
			ret = -EIO;
			K_SPINLOCK_BREAK;
		}

		for (spins = 0U; spins < AES_SPIN_LIMIT; spins++) {
			if ((*ctrl & AES_CTRL_DONE) != 0U) {
				break;
			}
		}

		if (spins == AES_SPIN_LIMIT) {
			ret = -EIO;
			K_SPINLOCK_BREAK;
		}

		for (unsigned int i = 0U; i < AES_BLOCK_WORDS; i++) {
			block[i] = *port;
		}
	}

	if (ret != 0) {
		LOG_ERR("AES engine did not complete (fed %u of %u words)", fed, AES_BLOCK_WORDS);
		return ret;
	}

	for (unsigned int i = 0U; i < AES_BLOCK_WORDS; i++) {
		uint8_t *q = &out[i * 4U];

		q[0] = (uint8_t)(block[i] & 0xffU);
		q[1] = (uint8_t)((block[i] >> 8) & 0xffU);
		q[2] = (uint8_t)((block[i] >> 16) & 0xffU);
		q[3] = (uint8_t)((block[i] >> 24) & 0xffU);
	}

	return 0;
}

static int crypto_tlsr8258_ecb_op(struct cipher_ctx *ctx, struct cipher_pkt *pkt)
{
	const struct crypto_tlsr8258_session *session = ctx->drv_sessn_state;
	uint8_t *out;
	int ret;

	if (pkt->in_len != (int)AES_BLOCK_SIZE) {
		LOG_ERR("ECB only operates on single %u-byte blocks", AES_BLOCK_SIZE);
		return -EINVAL;
	}

	out = (pkt->out_buf != NULL) ? pkt->out_buf : pkt->in_buf;

	if ((pkt->out_buf != NULL) && (pkt->out_buf_max < (int)AES_BLOCK_SIZE)) {
		LOG_ERR("output buffer too small");
		return -EINVAL;
	}

	/* The whole input is latched before any output is produced, so an
	 * in-place call is safe.
	 */
	ret = crypto_tlsr8258_block(ctx->device, session, pkt->in_buf, out);
	if (ret != 0) {
		return ret;
	}

	pkt->out_len = (int)AES_BLOCK_SIZE;

	return 0;
}

static int crypto_tlsr8258_query_caps(const struct device *dev)
{
	ARG_UNUSED(dev);

	return AES_CAPS;
}

static int crypto_tlsr8258_begin_session(const struct device *dev, struct cipher_ctx *ctx,
					 enum cipher_algo algo, enum cipher_mode mode,
					 enum cipher_op op_type)
{
	struct crypto_tlsr8258_data *data = dev->data;
	struct crypto_tlsr8258_session *session = NULL;

	if (algo != CRYPTO_CIPHER_ALGO_AES) {
		return -ENOTSUP;
	}

	if (mode != CRYPTO_CIPHER_MODE_ECB) {
		return -ENOTSUP;
	}

	if (ctx->keylen != AES_KEY_SIZE) {
		return -ENOTSUP;
	}

	if ((ctx->flags & ~(uint16_t)AES_CAPS) != 0U) {
		return -ENOTSUP;
	}

	if ((ctx->flags & CAP_SYNC_OPS) == 0U) {
		return -ENOTSUP;
	}

	if (ctx->key.bit_stream == NULL) {
		LOG_ERR("no key provided");
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		ARRAY_FOR_EACH_PTR(data->sessions, s) {
			if (!s->in_use) {
				s->in_use = true;
				session = s;
				break;
			}
		}
	}

	if (session == NULL) {
		LOG_ERR("all %d sessions in use", CONFIG_CRYPTO_TLSR8258_MAX_SESSION);
		return -EBUSY;
	}

	memcpy(session->key, ctx->key.bit_stream, AES_KEY_SIZE);
	session->decrypt = (op_type == CRYPTO_CIPHER_OP_DECRYPT);

	ctx->drv_sessn_state = session;
	ctx->ops.block_crypt_hndlr = crypto_tlsr8258_ecb_op;
	ctx->ops.cipher_mode = mode;

	return 0;
}

static int crypto_tlsr8258_free_session(const struct device *dev, struct cipher_ctx *ctx)
{
	struct crypto_tlsr8258_data *data = dev->data;
	struct crypto_tlsr8258_session *session = ctx->drv_sessn_state;

	if (session == NULL) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		memset(session->key, 0, AES_KEY_SIZE);
		session->in_use = false;
	}

	ctx->drv_sessn_state = NULL;

	return 0;
}

static int crypto_tlsr8258_init(const struct device *dev)
{
	struct crypto_tlsr8258_data *data = dev->data;

	ARRAY_FOR_EACH_PTR(data->sessions, s) {
		s->in_use = false;
	}

	return 0;
}

static DEVICE_API(crypto, crypto_tlsr8258_api) = {
	.query_hw_caps = crypto_tlsr8258_query_caps,
	.cipher_begin_session = crypto_tlsr8258_begin_session,
	.cipher_free_session = crypto_tlsr8258_free_session,
};

#define CRYPTO_TLSR8258_INIT(n)								\
	static const struct crypto_tlsr8258_config crypto_tlsr8258_cfg_##n = {		\
		.base = (uintptr_t)DT_INST_REG_ADDR(n),					\
	};										\
											\
	static struct crypto_tlsr8258_data crypto_tlsr8258_data_##n;			\
											\
	DEVICE_DT_INST_DEFINE(n, crypto_tlsr8258_init, NULL,				\
			      &crypto_tlsr8258_data_##n, &crypto_tlsr8258_cfg_##n,	\
			      POST_KERNEL, CONFIG_CRYPTO_INIT_PRIORITY,			\
			      &crypto_tlsr8258_api);

DT_INST_FOREACH_STATUS_OKAY(CRYPTO_TLSR8258_INIT)
