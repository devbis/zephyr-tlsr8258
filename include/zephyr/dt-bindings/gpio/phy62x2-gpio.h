/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The pads of the PHY62x2 as the cells of its GPIO controller. The controller counts hardware indices, not the
 * pad numbers printed in the datasheet: the index is the position of the pad in this list (P0, P1, P2, P3, P7, P9,
 * P10, P11, P14, P15, P16, P17, P18, P20, P23, P24, P25, P26, P27, P31, P32, P33, P34), so P15 is 9 and P7 is 4.
 *
 *   <&gpio0 PHY62X2_P15 GPIO_ACTIVE_HIGH>
 *
 * A module does not expose every pad, and some are taken (the console, the crystal): see its board file.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_PHY62X2_GPIO_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_PHY62X2_GPIO_H_

#define PHY62X2_P0 0
#define PHY62X2_P1 1
#define PHY62X2_P2 2
#define PHY62X2_P3 3
#define PHY62X2_P7 4
#define PHY62X2_P9 5
#define PHY62X2_P10 6
#define PHY62X2_P11 7
#define PHY62X2_P14 8
#define PHY62X2_P15 9
#define PHY62X2_P16 10
#define PHY62X2_P17 11
#define PHY62X2_P18 12
#define PHY62X2_P20 13
#define PHY62X2_P23 14
#define PHY62X2_P24 15
#define PHY62X2_P25 16
#define PHY62X2_P26 17
#define PHY62X2_P27 18
#define PHY62X2_P31 19
#define PHY62X2_P32 20
#define PHY62X2_P33 21
#define PHY62X2_P34 22

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_PHY62X2_GPIO_H_ */
