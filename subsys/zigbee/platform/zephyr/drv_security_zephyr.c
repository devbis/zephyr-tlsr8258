/* SPDX-License-Identifier: Apache-2.0 */
/*
 * AES-128 driver for TLSR8258 using the built-in hardware AES accelerator.
 * Register layout (base 0x00800000):
 *   0x540  reg_aes_ctrl  (u8)  bit0=CODEC_TRIG(0=enc,1=dec), bit1=DATA_FEED, bit2=CODEC_FINISHED
 *   0x548  reg_aes_data  (u32) feed/read 4×u32
 *   0x550  reg_aes_key[] (u8)  16-byte key
 */
#include <zephyr/zigbee/zb_types.h>
#include "drv_security.h"

#define TLSR_REG8(a)   (*(volatile u8  *)(0x00800000u + (a)))
#define TLSR_REG32(a)  (*(volatile u32 *)(0x00800000u + (a)))

#define REG_AES_CTRL        TLSR_REG8(0x540)
#define REG_AES_DATA        TLSR_REG32(0x548)
#define REG_AES_KEY(i)      TLSR_REG8(0x550u + (i))

#define AES_TRIG_ENCRYPT    0u
#define AES_TRIG_DECRYPT    BIT(0)
#define AES_DATA_FEED       BIT(1)
#define AES_FINISHED        BIT(2)

static void _aes_run(u8 mode, const u8 *key, const u8 *in, u8 *out)
{
	/* Arm codec direction */
	if (mode == AES_TRIG_ENCRYPT) {
		REG_AES_CTRL &= ~AES_TRIG_DECRYPT;
	} else {
		REG_AES_CTRL |= AES_TRIG_DECRYPT;
	}

	/* Load 16-byte key */
	for (int i = 0; i < 16; i++) {
		REG_AES_KEY(i) = key[i];
	}

	/* Feed 16-byte input as 4 × u32 little-endian words */
	const u8 *p = in;

	while (REG_AES_CTRL & AES_DATA_FEED) {
		u32 w = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);

		REG_AES_DATA = w;
		p += 4;
	}

	/* Wait for completion */
	while (!(REG_AES_CTRL & AES_FINISHED)) {
	}

	/* Read 16-byte result as 4 × u32 */
	u8 *q = out;

	for (int i = 0; i < 4; i++) {
		u32 w = REG_AES_DATA;

		*q++ = (u8)(w & 0xff);
		*q++ = (u8)((w >> 8) & 0xff);
		*q++ = (u8)((w >> 16) & 0xff);
		*q++ = (u8)((w >> 24) & 0xff);
	}
}

void drv_aes_encrypt(u8 *key, u8 *plain, u8 *result)
{
	_aes_run(AES_TRIG_ENCRYPT, key, plain, result);
}

void drv_aes_decrypt(u8 *key, u8 *cipher, u8 *result)
{
	_aes_run(AES_TRIG_DECRYPT, key, cipher, result);
}

