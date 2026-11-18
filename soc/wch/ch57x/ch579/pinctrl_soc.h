/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_WCH_CH57X_CH579_PINCTRL_SOC_H_
#define ZEPHYR_SOC_WCH_CH57X_CH579_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/pinctrl/ch57x-pinctrl.h>
#include <zephyr/types.h>

/** @cond INTERNAL_HIDDEN */

typedef struct pinctrl_soc_pin {
	uint16_t pinmux;
	uint8_t bias_pull_up: 1;
	uint8_t bias_pull_down: 1;
	uint8_t output_high: 1;
	uint8_t output_low: 1;
} pinctrl_soc_pin_t;

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	{                                                                                          \
		.pinmux = DT_PROP_BY_IDX(node_id, prop, idx),                                      \
		.bias_pull_up = DT_PROP(node_id, bias_pull_up),                                    \
		.bias_pull_down = DT_PROP(node_id, bias_pull_down),                                \
		.output_high = DT_PROP(node_id, output_high),                                      \
		.output_low = DT_PROP(node_id, output_low),                                        \
	},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop), DT_FOREACH_PROP_ELEM, pinmux,           \
				Z_PINCTRL_STATE_PIN_INIT)}

/** @endcond */

#endif /* ZEPHYR_SOC_WCH_CH57X_CH579_PINCTRL_SOC_H_ */
