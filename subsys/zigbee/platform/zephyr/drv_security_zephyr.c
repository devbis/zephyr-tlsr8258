/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/zigbee/zb_types.h>
#include <tinycrypt/aes.h>
#include <tinycrypt/constants.h>
#include "drv_security.h"

void drv_aes_encrypt(u8 *key, u8 *plain, u8 *result)
{
	struct tc_aes_key_sched_struct sched;

	tc_aes128_set_encrypt_key(&sched, key);
	tc_aes_encrypt(result, plain, &sched);
}

void drv_aes_decrypt(u8 *key, u8 *cipher, u8 *result)
{
	struct tc_aes_key_sched_struct sched;

	tc_aes128_set_decrypt_key(&sched, key);
	tc_aes_decrypt(result, cipher, &sched);
}
