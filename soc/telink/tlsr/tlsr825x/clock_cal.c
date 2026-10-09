/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#include "clock_cal.h"

#define TLSR8258_REG_ANA_ADDR (*(volatile uint8_t *)0x008000b8u)
#define TLSR8258_REG_ANA_DATA (*(volatile uint8_t *)0x008000b9u)
#define TLSR8258_REG_ANA_CTRL (*(volatile uint8_t *)0x008000bau)

#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

/* Analog registers used by the RC calibration sequences. */
#define TLSR8258_ANA_CLK_32K_SEL   0x2du
#define TLSR8258_ANA_CLK_32K_PWR   0x05u
#define TLSR8258_ANA_CAP_32K_MAN   0x32u
#define TLSR8258_ANA_RES_32K_MAN   0x31u
#define TLSR8258_ANA_CAL_32K_CTRL  0x30u
#define TLSR8258_ANA_CAL_32K_START 0xc6u
#define TLSR8258_ANA_CAL_32K_RES_H 0xc9u
#define TLSR8258_ANA_CAL_32K_RES_L 0xcau
#define TLSR8258_ANA_CAL_STATUS    0xcfu
#define TLSR8258_ANA_CAL_24M_CAP   0xcbu
#define TLSR8258_ANA_CAL_24M_EN    0xc8u
#define TLSR8258_ANA_CAL_24M_MAN   0x33u
#define TLSR8258_ANA_CAL_RC48_CAP  0x33u
#define TLSR8258_ANA_CAL_RC48_EN   0x30u
#define TLSR8258_ANA_CAL_RC48_CTRL 0xc7u
#define TLSR8258_ANA_CAL_RC48_CNT0 0xceu
#define TLSR8258_ANA_CAL_RC48_CNT1 0xcfu

#define TLSR8258_ANA_DUTY_EN       0x86u
#define TLSR8258_ANA_DUTY_CTRL     0x82u
#define TLSR8258_ANA_DUTY_CAL      0x87u
#define TLSR8258_ANA_DUTY_RESULT   0x88u

/* Bounded wait for the calibration engines: a stuck engine must not hang boot. */
#define TLSR8258_CAL_WAIT_MAX_POLLS 100000u

uint8_t tlsr8258_analog_read(uint8_t addr)
{
	unsigned int key = irq_lock();
	uint8_t value;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
	value = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);

	return value;
}

void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);
}

/* Poll the calibration status register until bit 'mask' is set or the budget ends. */
static bool tlsr8258_cal_wait_bit(uint8_t reg, uint8_t mask)
{
	for (uint32_t i = 0u; i < TLSR8258_CAL_WAIT_MAX_POLLS; i++) {
		if ((tlsr8258_analog_read(reg) & mask) != 0u) {
			return true;
		}
	}

	return false;
}

void tlsr8258_clock_32k_rc_init(void)
{
	uint8_t sel_32k = tlsr8258_analog_read(TLSR8258_ANA_CLK_32K_SEL) & 0x7fu;
	uint8_t power_32k = tlsr8258_analog_read(TLSR8258_ANA_CLK_32K_PWR) & 0xfcu;

	tlsr8258_analog_write(TLSR8258_ANA_CLK_32K_SEL, sel_32k);
	/* Power-up code 0x2 selects the 32 kHz RC oscillator. */
	tlsr8258_analog_write(TLSR8258_ANA_CLK_32K_PWR, power_32k | 0x2u);
}

void tlsr8258_rc_32k_cal(void)
{
	uint8_t res;

	tlsr8258_analog_write(TLSR8258_ANA_CAL_32K_CTRL, 0x60u);
	tlsr8258_analog_write(TLSR8258_ANA_CAL_32K_START, 0xf6u);
	tlsr8258_analog_write(TLSR8258_ANA_CAL_32K_START, 0xf7u);
	if (!tlsr8258_cal_wait_bit(TLSR8258_ANA_CAL_STATUS, BIT(6))) {
		/* Keep the free-running 32 kHz RC on its default trim. */
		tlsr8258_analog_write(TLSR8258_ANA_CAL_32K_START, 0xf6u);
		tlsr8258_analog_write(TLSR8258_ANA_CAL_32K_CTRL, 0x20u);
		return;
	}

	res = tlsr8258_analog_read(TLSR8258_ANA_CAL_32K_RES_H);
	tlsr8258_analog_write(TLSR8258_ANA_CAP_32K_MAN, res);
	res = tlsr8258_analog_read(TLSR8258_ANA_CAL_32K_RES_L);
	tlsr8258_analog_write(TLSR8258_ANA_RES_32K_MAN, res);

	tlsr8258_analog_write(TLSR8258_ANA_CAL_32K_START, 0xf6u);
	tlsr8258_analog_write(TLSR8258_ANA_CAL_32K_CTRL, 0x20u);
}

void tlsr8258_rc_24m_cal(void)
{
	uint8_t cap;

	tlsr8258_analog_write(TLSR8258_ANA_CAL_24M_EN, 0x80u);
	/* Manual-on switch for the trim, then start the counter. */
	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_EN,
			      tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_EN) | BIT(7));
	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CTRL, 0x0eu);
	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CTRL, 0x0fu);
	if (!tlsr8258_cal_wait_bit(TLSR8258_ANA_CAL_STATUS, BIT(7))) {
		tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_EN,
				      tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_EN) & (uint8_t)~BIT(7));
		return;
	}

	cap = tlsr8258_analog_read(TLSR8258_ANA_CAL_24M_CAP);
	tlsr8258_analog_write(TLSR8258_ANA_CAL_24M_MAN, cap);

	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_EN,
			      tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_EN) & (uint8_t)~BIT(7));
	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CTRL, 0x0eu);
}

void tlsr8258_rc_48m_cal(void)
{
	uint32_t cal_cnt;
	uint32_t f = 64u;
	uint32_t temp_d2 = 100u;
	uint8_t temp_cap = 0u;

	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CAP, 0x80u);
	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_EN, 0x20u);

	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CTRL, 0x0eu);
	k_busy_wait(1000u);
	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CTRL, 0x0fu);
	if (!tlsr8258_cal_wait_bit(TLSR8258_ANA_CAL_STATUS, BIT(7))) {
		return;
	}

	cal_cnt = tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_CNT1) & 0x07u;
	cal_cnt = (cal_cnt << 8) + tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_CNT0);

	while (f >= 1u) {
		uint8_t temp_v = tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_CAP);
		uint32_t temp_d;

		temp_d = (cal_cnt > 250u) ? (cal_cnt - 250u) : (250u - cal_cnt);
		if (cal_cnt > 250u) {
			tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CAP, (uint8_t)(temp_v - f));
		} else {
			tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CAP, (uint8_t)(temp_v + f));
		}
		f = f / 2u;

		tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CTRL, 0x0eu);
		tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CTRL, 0x0fu);
		if (!tlsr8258_cal_wait_bit(TLSR8258_ANA_CAL_STATUS, BIT(7))) {
			break;
		}
		cal_cnt = tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_CNT1) & 0x07u;
		cal_cnt = (cal_cnt << 8) + tlsr8258_analog_read(TLSR8258_ANA_CAL_RC48_CNT0);

		if (temp_d2 > temp_d) {
			temp_d2 = temp_d;
			temp_cap = temp_v;
		}
	}

	tlsr8258_analog_write(TLSR8258_ANA_CAL_RC48_CAP, temp_cap);
}

/*
 * The vendor build places this routine in RAM. It is not placed there here
 * because the analog helpers it calls live in flash text; it is not called
 * by the port and must be moved to .ram_code before any use.
 */
void tlsr8258_doubler_calibration(void)
{
	uint8_t tmp;
	uint8_t dcal;

	tlsr8258_analog_write(TLSR8258_ANA_DUTY_EN, 0xbbu);
	tlsr8258_analog_write(TLSR8258_ANA_DUTY_CTRL,
			      tlsr8258_analog_read(TLSR8258_ANA_DUTY_CTRL) & 0x7fu);
	tlsr8258_analog_write(TLSR8258_ANA_DUTY_CAL,
			      (tlsr8258_analog_read(TLSR8258_ANA_DUTY_CAL) & 0xfcu) | 0x02u);
	tlsr8258_analog_write(TLSR8258_ANA_DUTY_CAL,
			      tlsr8258_analog_read(TLSR8258_ANA_DUTY_CAL) | 0x04u);

	tmp = tlsr8258_analog_read(TLSR8258_ANA_DUTY_RESULT);
	dcal = tmp & 0x1fu;
	tlsr8258_analog_write(TLSR8258_ANA_DUTY_CAL,
			      (tlsr8258_analog_read(TLSR8258_ANA_DUTY_CAL) & 0x07u) | (dcal << 3));

	tlsr8258_analog_write(TLSR8258_ANA_DUTY_CTRL,
			      tlsr8258_analog_read(TLSR8258_ANA_DUTY_CTRL) | 0x80u);
	tlsr8258_analog_write(TLSR8258_ANA_DUTY_CAL,
			      tlsr8258_analog_read(TLSR8258_ANA_DUTY_CAL) & 0xfdu);
	tlsr8258_analog_write(TLSR8258_ANA_DUTY_EN, 0xfbu);
}
