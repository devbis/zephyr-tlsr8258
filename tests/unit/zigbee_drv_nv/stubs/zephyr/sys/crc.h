/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TEST_STUB_CRC_H_
#define TEST_STUB_CRC_H_

#include <stddef.h>
#include <stdint.h>

uint8_t crc8_ccitt(uint8_t seed, const void *src, size_t len);

#endif
