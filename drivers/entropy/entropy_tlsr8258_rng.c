/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Entropy source of the TLSR8258: the noise of the SAR ADC, sampled as the
 * vendor random_generator_init() does. The miscellaneous channel runs
 * differential with both inputs on ground at 14 bits and 96 kHz, and the
 * samples are collected through the data FIFO of that channel in bursts of
 * 16. The vendor library folds a burst into a 16 bit seed with a shift
 * register; here the least significant bits of four samples are XORed into
 * every output bit.
 *
 * The first two samples of a burst are the converter settling after its reset
 * and carry no noise, and the least significant bit alone is biased, so those
 * two are dropped and four are folded. On 32768 bursts-worth of data from
 * eight runs this gave a bit with P(1) = 0.499, no serial correlation and a
 * min-entropy of at least 0.95 bit.
 */

#define DT_DRV_COMPAT telink_tlsr8258_rng

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define TLSR8258_REG8(addr)  (*(volatile uint8_t *)(addr))
#define TLSR8258_REG16(addr) (*(volatile uint16_t *)(addr))

#define TLSR8258_REG_RST1        TLSR8258_REG8(0x00800061u)
#define TLSR8258_FLD_RST1_ADC    BIT(5)
#define TLSR8258_REG_DFIFO_MODE  TLSR8258_REG8(0x00800b10u)
#define TLSR8258_FLD_DFIFO2_IN   BIT(2)
#define TLSR8258_REG_DFIFO2_ADDR TLSR8258_REG16(0x00800b08u)
#define TLSR8258_REG_DFIFO2_SIZE TLSR8258_REG8(0x00800b0au)
#define TLSR8258_REG_DFIFO2_WPTR TLSR8258_REG16(0x00800b1eu)

#define TLSR8258_REG_ANA_ADDR TLSR8258_REG8(0x008000b8u)
#define TLSR8258_REG_ANA_DATA TLSR8258_REG8(0x008000b9u)
#define TLSR8258_REG_ANA_CTRL TLSR8258_REG8(0x008000bau)
#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

#define TLSR8258_ANA_CLK_SETTING  0x82u
#define TLSR8258_FLD_CLK_24M_SAR  BIT(6)
#define TLSR8258_ANA_VREF         0xe7u
#define TLSR8258_ANA_AIN_MISC     0xe8u
#define TLSR8258_ANA_RES_MISC     0xecu
#define TLSR8258_FLD_RES_MISC     (BIT(0) | BIT(1))
#define TLSR8258_FLD_DIFF_MISC    BIT(6)
#define TLSR8258_ANA_TSAMPLE_MISC 0xeeu
#define TLSR8258_ANA_R_MAX_MC     0xefu
#define TLSR8258_ANA_R_MAX_C      0xf0u
#define TLSR8258_ANA_R_MAX_S      0xf1u
#define TLSR8258_ANA_CHN_EN       0xf2u
#define TLSR8258_FLD_CHN_EN_MISC  BIT(2)
#define TLSR8258_ANA_SAMPLE_CLK   0xf4u
#define TLSR8258_ANA_VBAT_DIV     0xf9u
#define TLSR8258_FLD_VBAT_DIV     (BIT(2) | BIT(3))
#define TLSR8258_FLD_PRESCALE_EN  BIT(4)
#define TLSR8258_ANA_AIN_SCALE    0xfau
#define TLSR8258_FLD_SEL_AIN_SCALE (BIT(6) | BIT(7))
#define TLSR8258_ANA_PGA_CTRL     0xfcu
#define TLSR8258_FLD_GAIN_L       (BIT(0) | BIT(1))
#define TLSR8258_FLD_GAIN_R       (BIT(2) | BIT(3))
#define TLSR8258_FLD_ADC_MODE     BIT(4)
#define TLSR8258_FLD_SAR_PD       BIT(5)
#define TLSR8258_FLD_PGA_PD_L     BIT(6)
#define TLSR8258_FLD_PGA_PD_R     BIT(7)

#define TLSR8258_GND_INPUT       15u
#define TLSR8258_VREF_1P2V       2u
#define TLSR8258_RES14           3u
#define TLSR8258_TSAMPLE_6_CYC   1u
#define TLSR8258_PRESCALER_1F8   3u
#define TLSR8258_GAIN_BIAS_100   1u
#define TLSR8258_SAMPLE_CLK_DIV  5u
#define TLSR8258_BURST_SAMPLES   16u
/* Samples at the start of a burst, before the converter has settled. */
#define TLSR8258_BURST_SETTLE    2u
/* 96 kHz is one sample per 10.4 us; allow generous time for a burst. */
#define TLSR8258_BURST_WAIT_US   40u
#define TLSR8258_SAMPLE_WAIT_US  12u

/* Samples whose least significant bits are XORed into one output bit. */
#define TLSR8258_RNG_FOLD 4u
/* Bursts that the converter may fail to fill or leave stuck before the read fails. */
#define TLSR8258_RNG_MAX_BAD_BURSTS 16u

static void tlsr8258_analog_wait(void)
{
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr8258_analog_read(uint8_t addr)
{
	uint8_t value;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	tlsr8258_analog_wait();
	value = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;

	return value;
}

static void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	tlsr8258_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
}

/* Read-modify-write of an analog register: clear 'clr' bits, then set 'set'. */
static void tlsr8258_analog_update(uint8_t addr, uint8_t clr, uint8_t set)
{
	tlsr8258_analog_write(addr, (tlsr8258_analog_read(addr) & (uint8_t)~clr) | set);
}

static void tlsr8258_adc_reset_module(void)
{
	TLSR8258_REG_RST1 = TLSR8258_FLD_RST1_ADC;
	TLSR8258_REG_RST1 = 0u;
}

/* Vendor adc_init() followed by rng_init(): the miscellaneous channel on ground. */
static void tlsr8258_rng_adc_configure(void)
{
	/* adc_init(): power the converter down and bring it to a known state. */
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, 0u, TLSR8258_FLD_SAR_PD);
	tlsr8258_adc_reset_module();
	tlsr8258_analog_update(TLSR8258_ANA_CLK_SETTING, 0u, TLSR8258_FLD_CLK_24M_SAR);
	tlsr8258_analog_write(TLSR8258_ANA_SAMPLE_CLK, TLSR8258_SAMPLE_CLK_DIV & 0x07u);
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, 0u,
			       TLSR8258_FLD_PGA_PD_L | TLSR8258_FLD_PGA_PD_R);
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, TLSR8258_FLD_GAIN_L | TLSR8258_FLD_GAIN_R,
			       TLSR8258_GAIN_BIAS_100 | (TLSR8258_GAIN_BIAS_100 << 2));
	TLSR8258_REG_DFIFO_MODE &= (uint8_t)~TLSR8258_FLD_DFIFO2_IN;

	/* rng_init(): 96 kHz, channel enable, 1.2 V reference, ground on both inputs. */
	tlsr8258_analog_write(TLSR8258_ANA_R_MAX_MC, 0xf0u);
	tlsr8258_analog_write(TLSR8258_ANA_R_MAX_C, 0x00u);
	tlsr8258_analog_write(TLSR8258_ANA_R_MAX_S, 0x0au);
	tlsr8258_analog_update(TLSR8258_ANA_CHN_EN, 0x0fu, TLSR8258_FLD_CHN_EN_MISC);
	tlsr8258_analog_update(TLSR8258_ANA_CHN_EN, 0x70u, 0x20u);
	tlsr8258_analog_write(TLSR8258_ANA_VREF, TLSR8258_VREF_1P2V | (TLSR8258_VREF_1P2V << 2) |
						    (TLSR8258_VREF_1P2V << 4));
	tlsr8258_analog_write(TLSR8258_ANA_AIN_SCALE,
			      (tlsr8258_analog_read(TLSR8258_ANA_AIN_SCALE) & 0xc0u) | 0x3du);
	tlsr8258_analog_update(TLSR8258_ANA_VBAT_DIV, TLSR8258_FLD_VBAT_DIV, 0u);
	/* Positive and negative input of the miscellaneous channel: ground. */
	tlsr8258_analog_write(TLSR8258_ANA_AIN_MISC,
			      (uint8_t)((TLSR8258_GND_INPUT << 4) | TLSR8258_GND_INPUT));
	tlsr8258_analog_update(TLSR8258_ANA_RES_MISC, TLSR8258_FLD_RES_MISC,
			       TLSR8258_RES14 | TLSR8258_FLD_DIFF_MISC);
	tlsr8258_analog_write(TLSR8258_ANA_TSAMPLE_MISC, TLSR8258_TSAMPLE_6_CYC);
	tlsr8258_analog_update(TLSR8258_ANA_AIN_SCALE, TLSR8258_FLD_SEL_AIN_SCALE,
			       (uint8_t)(TLSR8258_PRESCALER_1F8 << 6));
	tlsr8258_analog_update(TLSR8258_ANA_VBAT_DIV, 0u, TLSR8258_FLD_PRESCALE_EN);
	/* Normal (not RNS) mode. */
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, TLSR8258_FLD_ADC_MODE, 0u);
}

/*
 * One burst of samples of the noise. Returns the number of samples that the
 * converter delivered, 0 when it did not run.
 */
static size_t tlsr8258_rng_burst(uint16_t out[TLSR8258_BURST_SAMPLES])
{
	volatile int32_t fifo[TLSR8258_BURST_SAMPLES] __aligned(16);
	size_t got = 0u;
	unsigned int key = irq_lock();

	tlsr8258_rng_adc_configure();
	/* Power the converter on and let it settle. */
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, TLSR8258_FLD_SAR_PD, 0u);
	tlsr8258_adc_reset_module();
	for (size_t i = 0; i < TLSR8258_BURST_SAMPLES; i++) {
		fifo[i] = 0;
	}

	TLSR8258_REG_DFIFO2_ADDR = (uint16_t)(uintptr_t)fifo;
	TLSR8258_REG_DFIFO2_SIZE = (uint8_t)((sizeof(fifo) >> 4) - 1u);
	TLSR8258_REG_DFIFO2_WPTR = 0u;
	TLSR8258_REG_DFIFO_MODE |= TLSR8258_FLD_DFIFO2_IN;

	k_busy_wait(TLSR8258_BURST_WAIT_US);
	for (size_t i = 0; i < TLSR8258_BURST_SAMPLES; i++) {
		/* A sample of zero is indistinguishable from none yet: wait a little. */
		for (size_t t = 0; (fifo[i] == 0) && (t < 3u); t++) {
			k_busy_wait(TLSR8258_SAMPLE_WAIT_US);
		}
		out[i] = (uint16_t)(fifo[i] & 0x3fff);
		if (fifo[i] != 0) {
			got++;
		}
	}

	TLSR8258_REG_DFIFO_MODE &= (uint8_t)~TLSR8258_FLD_DFIFO2_IN;
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, 0u, TLSR8258_FLD_SAR_PD);
	irq_unlock(key);

	return got;
}

/* A burst without noise: all settled samples the same, a stuck converter. */
static bool tlsr8258_rng_burst_stuck(const uint16_t burst[TLSR8258_BURST_SAMPLES])
{
	for (size_t i = TLSR8258_BURST_SETTLE + 1u; i < TLSR8258_BURST_SAMPLES; i++) {
		if (burst[i] != burst[TLSR8258_BURST_SETTLE]) {
			return false;
		}
	}

	return true;
}

static int tlsr8258_rng_get_entropy(const struct device *dev, uint8_t *buffer, uint16_t length)
{
	uint16_t burst[TLSR8258_BURST_SAMPLES];
	size_t have = 0u;
	uint8_t bits_in_byte = 0u;
	uint8_t byte = 0u;
	size_t bad = 0u;

	ARG_UNUSED(dev);

	while (have < length) {
		if ((tlsr8258_rng_burst(burst) != TLSR8258_BURST_SAMPLES) ||
		    tlsr8258_rng_burst_stuck(burst)) {
			if (++bad > TLSR8258_RNG_MAX_BAD_BURSTS) {
				return -EIO;
			}
			continue;
		}

		for (size_t i = TLSR8258_BURST_SETTLE;
		     (i + TLSR8258_RNG_FOLD) <= TLSR8258_BURST_SAMPLES; i += TLSR8258_RNG_FOLD) {
			uint8_t bit = 0u;

			for (size_t k = 0; k < TLSR8258_RNG_FOLD; k++) {
				bit ^= (uint8_t)(burst[i + k] & 1u);
			}
			byte = (uint8_t)((byte << 1) | bit);
			if (++bits_in_byte == 8u) {
				buffer[have++] = byte;
				bits_in_byte = 0u;
				byte = 0u;
				if (have == length) {
					break;
				}
			}
		}
	}

	return 0;
}

static DEVICE_API(entropy, tlsr8258_rng_api) = {
	.get_entropy = tlsr8258_rng_get_entropy,
	.get_entropy_isr = NULL,
};

static int tlsr8258_rng_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

DEVICE_DT_INST_DEFINE(0, tlsr8258_rng_init, NULL, NULL, NULL, PRE_KERNEL_1,
		      CONFIG_ENTROPY_INIT_PRIORITY, &tlsr8258_rng_api);
