/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_TELINK_TLSR825X_PINCTRL_SOC_H_
#define SOC_TELINK_TLSR825X_PINCTRL_SOC_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/pinctrl/tlsr8258-pinctrl.h>

typedef struct {
	uint32_t pinmux;
	uint8_t pull;
	bool input_enable;
	bool output_enable;
} pinctrl_soc_pin_t;

#define TLSR8258_PINMUX_PIN(pinmux)  ((pinmux) & 0xffffu)
#define TLSR8258_PINMUX_FUNC(pinmux) (((pinmux) >> 16) & 0xffu)

#define TLSR8258_PIN_PULL_FLOAT    0u
#define TLSR8258_PIN_PULL_UP_10K   3u
#define TLSR8258_PIN_PULL_DOWN_100K 2u

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)				\
	{									\
		.pinmux = DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx), pinmux),	\
		.pull = COND_CODE_1(						\
			DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx),		\
				bias_pull_up),					\
			(TLSR8258_PIN_PULL_UP_10K),				\
			(COND_CODE_1(						\
				DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx),	\
					bias_pull_down),			\
				(TLSR8258_PIN_PULL_DOWN_100K),		\
				(TLSR8258_PIN_PULL_FLOAT)))),		\
		.input_enable = DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx),	\
					input_enable),				\
		.output_enable = DT_PROP(DT_PROP_BY_IDX(node_id, prop, idx),	\
					 output_enable),			\
	},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) \
	{ DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT) }

#endif /* SOC_TELINK_TLSR825X_PINCTRL_SOC_H_ */
