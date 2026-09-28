/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_TELINK_TLSR825X_SOC_H_
#define SOC_TELINK_TLSR825X_SOC_H_

/* Register addresses shared by the startup assembly and C. */

/* Flash SPI master: data byte, control byte right above it. */
#define TLSR8258_REG_MSPI_DATA_ADDR   0x0080000c
#define TLSR8258_MSPI_CTRL_OFFSET     1
#define TLSR8258_MSPI_CTRL_CS         0x01

/* SPI flash command that releases the part from deep power-down. */
#define TLSR8258_FLASH_CMD_RELEASE_PD 0xab

/* Instruction cache: first locked line, then number of lines + 1. */
#define TLSR8258_REG_ICACHE_CTRL_ADDR 0x0080060c

#endif /* SOC_TELINK_TLSR825X_SOC_H_ */
