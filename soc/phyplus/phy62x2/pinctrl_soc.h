/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_PHYPLUS_PHY62X2_PINCTRL_SOC_H_
#define ZEPHYR_SOC_PHYPLUS_PHY62X2_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>
#include <zephyr/dt-bindings/pinctrl/phy62x2-pinctrl.h>

/** @cond INTERNAL_HIDDEN */

/* The two bits of the pull of a pad in the AON block */
#define PHY62X2_PULL_NONE 0U
#define PHY62X2_PULL_STRONG_UP 1U
#define PHY62X2_PULL_UP 2U
#define PHY62X2_PULL_DOWN 3U

typedef struct pinctrl_soc_pin {
	uint8_t pin;
	uint8_t function;
	uint8_t pull;
} pinctrl_soc_pin_t;

#define Z_PINCTRL_PHY62X2_PULL(node_id)                                                            \
	(DT_PROP(node_id, bias_pull_up) ?                                                          \
		 (DT_PROP(node_id, phyplus_strong_pull_up) ? PHY62X2_PULL_STRONG_UP :              \
							      PHY62X2_PULL_UP) :                   \
		 DT_PROP(node_id, bias_pull_down) ? PHY62X2_PULL_DOWN : PHY62X2_PULL_NONE)

#define Z_PINCTRL_PHY62X2_PINMUX(node_id) DT_PROP(node_id, pinmux)

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	{.pin = (Z_PINCTRL_PHY62X2_PINMUX(DT_PROP_BY_IDX(node_id, prop, idx)) >> 8) & 0xffU,       \
	 .function = Z_PINCTRL_PHY62X2_PINMUX(DT_PROP_BY_IDX(node_id, prop, idx)) & 0xffU,         \
	 .pull = Z_PINCTRL_PHY62X2_PULL(DT_PROP_BY_IDX(node_id, prop, idx))},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT)}

/** @endcond */

#endif /* ZEPHYR_SOC_PHYPLUS_PHY62X2_PINCTRL_SOC_H_ */
