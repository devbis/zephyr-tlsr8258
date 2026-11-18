/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_CH57X_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_CH57X_PINCTRL_H_

/**
 * Pin multiplexing description for the WCH CH57x family.
 *
 * Bits:
 * - [4:0]   pin number in the port
 * - [5]     port (0 = PA, 1 = PB)
 * - [6]     R16_PIN_ALTERNATE bit is to be updated
 * - [7]     value written to that R16_PIN_ALTERNATE bit
 * - [11:8]  R16_PIN_ALTERNATE bit index
 */
#define CH57X_PINMUX_PIN_SHIFT 0
#define CH57X_PINMUX_PORT_SHIFT 5
#define CH57X_PINMUX_ALT_VALID_SHIFT 6
#define CH57X_PINMUX_ALT_VAL_SHIFT 7
#define CH57X_PINMUX_ALT_BIT_SHIFT 8

#define CH57X_PORT_A 0
#define CH57X_PORT_B 1

/** Pin without any alternate-location bit (fixed function or plain GPIO) */
#define CH57X_PIN(port, pin)                                                                       \
	(((pin) << CH57X_PINMUX_PIN_SHIFT) | ((port) << CH57X_PINMUX_PORT_SHIFT))

/** Pin whose function location is selected by R16_PIN_ALTERNATE[bit] = val */
#define CH57X_PIN_ALT(port, pin, bit, val)                                                         \
	(CH57X_PIN(port, pin) | (1 << CH57X_PINMUX_ALT_VALID_SHIFT) |                              \
	 ((val) << CH57X_PINMUX_ALT_VAL_SHIFT) | ((bit) << CH57X_PINMUX_ALT_BIT_SHIFT))

/* R16_PIN_ALTERNATE bit indexes */
#define CH57X_ALT_TMR0 0
#define CH57X_ALT_TMR1 1
#define CH57X_ALT_TMR2 2
#define CH57X_ALT_TMR3 3
#define CH57X_ALT_UART0 4
#define CH57X_ALT_UART1 5
#define CH57X_ALT_UART2 6
#define CH57X_ALT_UART3 7
#define CH57X_ALT_SPI0 8

/* UART pins, default (main) location */
#define CH57X_UART0_TX_PB7 CH57X_PIN_ALT(CH57X_PORT_B, 7, CH57X_ALT_UART0, 0)
#define CH57X_UART0_RX_PB4 CH57X_PIN_ALT(CH57X_PORT_B, 4, CH57X_ALT_UART0, 0)
#define CH57X_UART1_TX_PA9 CH57X_PIN_ALT(CH57X_PORT_A, 9, CH57X_ALT_UART1, 0)
#define CH57X_UART1_RX_PA8 CH57X_PIN_ALT(CH57X_PORT_A, 8, CH57X_ALT_UART1, 0)
#define CH57X_UART2_TX_PA7 CH57X_PIN_ALT(CH57X_PORT_A, 7, CH57X_ALT_UART2, 0)
#define CH57X_UART2_RX_PA6 CH57X_PIN_ALT(CH57X_PORT_A, 6, CH57X_ALT_UART2, 0)
#define CH57X_UART3_TX_PA5 CH57X_PIN_ALT(CH57X_PORT_A, 5, CH57X_ALT_UART3, 0)
#define CH57X_UART3_RX_PA4 CH57X_PIN_ALT(CH57X_PORT_A, 4, CH57X_ALT_UART3, 0)

/* UART pins, alternate location */
#define CH57X_UART0_TX_PA14 CH57X_PIN_ALT(CH57X_PORT_A, 14, CH57X_ALT_UART0, 1)
#define CH57X_UART0_RX_PA15 CH57X_PIN_ALT(CH57X_PORT_A, 15, CH57X_ALT_UART0, 1)
#define CH57X_UART1_TX_PB9 CH57X_PIN_ALT(CH57X_PORT_B, 9, CH57X_ALT_UART1, 1)
#define CH57X_UART1_RX_PB8 CH57X_PIN_ALT(CH57X_PORT_B, 8, CH57X_ALT_UART1, 1)
#define CH57X_UART2_TX_PB23 CH57X_PIN_ALT(CH57X_PORT_B, 23, CH57X_ALT_UART2, 1)
#define CH57X_UART2_RX_PB22 CH57X_PIN_ALT(CH57X_PORT_B, 22, CH57X_ALT_UART2, 1)
#define CH57X_UART3_TX_PB21 CH57X_PIN_ALT(CH57X_PORT_B, 21, CH57X_ALT_UART3, 1)
#define CH57X_UART3_RX_PB20 CH57X_PIN_ALT(CH57X_PORT_B, 20, CH57X_ALT_UART3, 1)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_CH57X_PINCTRL_H_ */
