/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Self-contained AES-128 + CCM* for the host coordinator daemon (no Zephyr /
 * no external crypto lib). Mirrors the Zigbee NWK security used by the device
 * (subsys/zigbee/common/zb_minimal_ccm.c): CCM* with L=2 and a 4-byte MIC.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* AES-128 single-block ECB encrypt (key and in/out are 16 bytes). */
void coord_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/*
 * CCM* authenticate-and-encrypt. `msg` (msg_len bytes) is encrypted in place;
 * the 4-byte MIC is written to mic_out. `aad` (aad_len bytes) is authenticated
 * only. Returns msg_len + 4 on success, 0 on bad args.
 */
uint8_t coord_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13],
			  const uint8_t *aad, uint8_t aad_len,
			  uint8_t *msg, uint8_t msg_len, uint8_t mic_out[4]);

/*
 * CCM* decrypt-and-verify. `cipher` (cipher_len bytes) is decrypted in place;
 * the 4-byte `mic` is checked against the recomputed tag. Returns true if the
 * MIC matches.
 */
bool coord_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13],
		       uint8_t *cipher, uint8_t cipher_len,
		       const uint8_t *aad, uint8_t aad_len, const uint8_t mic[4]);
