/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT wch_ch57x_pinctrl

#include <zephyr/arch/cpu.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/pinctrl/ch57x-pinctrl.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* GPIO port register offsets, shared with the GPIO driver */
#define PORT_DIR    0x00U
#define PORT_OUT    0x08U
#define PORT_CLR    0x0cU
#define PORT_PU     0x10U
#define PORT_PD_DRV 0x14U

static const uintptr_t ch57x_port_base[] = {
	DT_REG_ADDR(DT_NODELABEL(gpioa)),
	DT_REG_ADDR(DT_NODELABEL(gpiob)),
};

static void ch57x_rmw32(uintptr_t addr, uint32_t mask, bool set)
{
	uint32_t val = sys_read32(addr);

	sys_write32(set ? (val | mask) : (val & ~mask), addr);
}

static void ch57x_configure_pin(uintptr_t alt_reg, const pinctrl_soc_pin_t *pin)
{
	uint32_t mux = pin->pinmux;
	uintptr_t port = ch57x_port_base[(mux >> CH57X_PINMUX_PORT_SHIFT) & 0x1U];
	uint32_t mask = BIT(mux & 0x1fU);

	if (pin->output_high || pin->output_low) {
		if (pin->output_high) {
			ch57x_rmw32(port + PORT_OUT, mask, true);
		} else {
			sys_write32(mask, port + PORT_CLR);
		}
		/* 5 mA push-pull output */
		ch57x_rmw32(port + PORT_PD_DRV, mask, false);
		ch57x_rmw32(port + PORT_DIR, mask, true);
	} else {
		ch57x_rmw32(port + PORT_DIR, mask, false);
		ch57x_rmw32(port + PORT_PU, mask, pin->bias_pull_up);
		ch57x_rmw32(port + PORT_PD_DRV, mask, pin->bias_pull_down);
	}

	if ((mux & BIT(CH57X_PINMUX_ALT_VALID_SHIFT)) != 0U) {
		uint16_t alt = sys_read16(alt_reg);
		uint16_t alt_mask = BIT((mux >> CH57X_PINMUX_ALT_BIT_SHIFT) & 0xfU);

		if ((mux & BIT(CH57X_PINMUX_ALT_VAL_SHIFT)) != 0U) {
			alt |= alt_mask;
		} else {
			alt &= ~alt_mask;
		}
		sys_write16(alt, alt_reg);
	}
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	for (uint8_t i = 0U; i < pin_cnt; i++) {
		ch57x_configure_pin(reg, &pins[i]);
	}

	return 0;
}
