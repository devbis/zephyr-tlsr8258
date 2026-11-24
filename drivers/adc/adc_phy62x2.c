/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT phyplus_phy62x2_adc

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/*
 * Ported from hal_adc_config_channel(), hal_adc_start() and hal_adc_stop() of the PHY62XX SDK
 * 3.1.1. The hardware channels are 2 to 7 for the single-ended inputs and 1, 3, 5 and 7 for the
 * differential pairs; the numbers are those of the SDK.
 */

#define ADC_BASE DT_INST_REG_ADDR(0)
#define ADC_INTR_MASK (ADC_BASE + 0x34U)
#define ADC_INTR_CLEAR (ADC_BASE + 0x38U)
#define ADC_INTR_STATUS (ADC_BASE + 0x3cU)
/* The samples of block n start at 0x400 and are 0x80 bytes apart, the first two words are not data */
#define ADC_BLOCK(n) (ADC_BASE + 0x400U + (n) * 0x80U)
#define ADC_BLOCK_FIRST_WORD 2U
#define ADC_SAMPLES 29U

#define SW_CLK 0x40000008U
#define SW_CLK_ADCC BIT(17)
#define AON_PMCTL2_1 0x4000f020U
#define AON_PULL_BASE 0x4000f008U
#define PCRM_CLKSEL 0x4000f03cU
#define PCRM_CLKHF_CTL0 0x4000f040U
#define PCRM_CLKHF_CTL1 0x4000f044U
#define PCRM_ANA_CTL 0x4000f048U
#define PCRM_ADC_CTL(n) (0x4000f06cU + 4U * (n))
#define ANALOG_IO_EN 0x40003800U
#define PAD_PS0 0x40003844U

#define ANA_CTL_LDO BIT(0)
#define ANA_CTL_ADC BIT(3)
#define ANA_CTL_MICBIAS BIT(23)
#define ADC_CTL_BIT4 BIT(4)
#define ADC_CTL_BIT20 BIT(20)

#define FIRST_CHANNEL 1U
#define LAST_CHANNEL 7U
#define CONVERSION_TIMEOUT_US 100000
#define VREF_MV 800

struct phy62x2_adc_channel {
	/* The pads of the input: the GPIO controller index of the positive and of the negative */
	uint8_t pin;
	uint8_t pin_negative;
	/* Bits of the control register of the resolution, for the input and for its pair */
	uint8_t aio;
	uint8_t aio_pair;
};

/* The indices of the pads in the GPIO controller: P11 7, P14 8, P15 9, P18 12, P20 13, P23 14, P24 15, P25 16 */
static const struct phy62x2_adc_channel channels[LAST_CHANNEL + 1U] = {
	[1] = {.pin = 12, .pin_negative = 16},
	[2] = {.pin = 7, .aio = 0, .aio_pair = 1},
	[3] = {.pin = 14, .pin_negative = 7, .aio = 1, .aio_pair = 0},
	[4] = {.pin = 15, .aio = 2, .aio_pair = 3},
	[5] = {.pin = 8, .pin_negative = 15, .aio = 3, .aio_pair = 2},
	[6] = {.pin = 9, .aio = 4, .aio_pair = 7},
	[7] = {.pin = 13, .pin_negative = 9, .aio = 7, .aio_pair = 4},
};

/* The two bits of a pad in the registers of the pull of the AON block: register and shift */
static const struct {
	uint8_t reg;
	uint8_t shift;
} pull[] = {
	[7] = {1, 4}, [8] = {1, 13}, [9] = {1, 16}, [12] = {1, 25},
	[13] = {2, 1}, [14] = {2, 10}, [15] = {2, 13}, [16] = {2, 16},
};

struct phy62x2_adc_data {
	struct k_mutex lock;
	/* Set by channel_setup() */
	uint8_t configured;
	uint8_t differential;
};

static void update_bits(uintptr_t reg, uint32_t mask, uint32_t value)
{
	sys_write32((sys_read32(reg) & ~mask) | (value & mask), reg);
}

static void pad_analog(uint8_t pin, bool enable, bool drive)
{
	if (enable) {
		/* Floating, the strong drive of the positive pad as the SDK sets it, analog function */
		update_bits(AON_PULL_BASE + 4U * pull[pin].reg, 3U << pull[pin].shift, 0U);
		if (drive) {
			update_bits(PAD_PS0, BIT(pin), BIT(pin));
		}
		update_bits(ANALOG_IO_EN, BIT(pin - 7U), BIT(pin - 7U));
	} else {
		update_bits(ANALOG_IO_EN, BIT(pin - 7U), 0U);
		update_bits(AON_PULL_BASE + 4U * pull[pin].reg, 3U << pull[pin].shift, 0U);
	}
}

static int phy62x2_adc_channel_setup(const struct device *dev, const struct adc_channel_cfg *cfg)
{
	struct phy62x2_adc_data *data = dev->data;
	uint8_t id = cfg->channel_id;

	if (id < FIRST_CHANNEL || id > LAST_CHANNEL || (id == 1U && !cfg->differential)) {
		return -EINVAL;
	}
	if (cfg->differential && (id % 2U) == 0U) {
		return -EINVAL;
	}
	if (cfg->gain != ADC_GAIN_1 || cfg->reference != ADC_REF_INTERNAL) {
		return -ENOTSUP;
	}

	data->configured |= BIT(id);
	if (cfg->differential) {
		data->differential |= BIT(id);
	} else {
		data->differential &= ~BIT(id);
	}
	return 0;
}

/*
 * The samples of a logical channel are in the block of its pair: the block 2 holds the channel 3.
 * The interrupt status shows the blocks that are ready.
 */
static uint32_t phy62x2_adc_blocks(uint8_t mask)
{
	uint32_t blocks = 0U;

	for (uint8_t id = 1U; id <= LAST_CHANNEL; id++) {
		if ((mask & BIT(id)) != 0U) {
			blocks |= BIT(id ^ 1U);
		}
	}
	return blocks;
}

static void phy62x2_adc_start(uint8_t mask, bool differential)
{
	uint32_t resolution = 0U;

	sys_set_bits(SW_CLK, SW_CLK_ADCC);
	sys_set_bits(PCRM_CLKSEL, BIT(6));
	sys_set_bits(PCRM_CLKHF_CTL0, BIT(18));
	sys_set_bits(PCRM_CLKHF_CTL1, BIT(7));
	sys_clear_bits(PCRM_CLKHF_CTL1, BIT(21));
	sys_set_bits(PCRM_CLKHF_CTL1, BIT(13));
	sys_set_bits(PCRM_ADC_CTL(4), ADC_CTL_BIT4 | BIT(0));

	/* The high resolution setting: input below 1 V, the bit of the input set and its bit of the divider cleared */
	for (uint8_t id = 2U; id <= LAST_CHANNEL; id++) {
		if ((mask & BIT(id)) != 0U) {
			resolution |= BIT(channels[id].aio);
			if (differential) {
				resolution |= BIT(channels[id].aio_pair);
			}
		}
	}
	sys_write32(resolution, AON_PMCTL2_1);

	for (uint32_t n = 0U; n < 4U; n++) {
		sys_clear_bits(PCRM_ADC_CTL(n), ADC_CTL_BIT20 | ADC_CTL_BIT4);
	}
	sys_clear_bits(PCRM_ANA_CTL, ANA_CTL_MICBIAS);

	if (!differential) {
		/* Automatic mode, the inputs to the sampler */
		sys_clear_bits(PCRM_ADC_CTL(4), ADC_CTL_BIT4);
		for (uint8_t id = 2U; id <= LAST_CHANNEL; id++) {
			if ((mask & BIT(id)) != 0U) {
				pad_analog(channels[id].pin, true, true);
				sys_set_bits(PCRM_ADC_CTL(1U + (id - 2U) / 2U),
					     (id % 2U) == 0U ? ADC_CTL_BIT20 : ADC_CTL_BIT4);
			}
		}
	} else {
		uint8_t id = find_lsb_set(mask) - 1U;

		/* The pair of the pads of the channel, and the selector of the pair */
		pad_analog(channels[id].pin, true, true);
		pad_analog(channels[id].pin_negative, true, false);
		update_bits(PCRM_ANA_CTL, GENMASK(7, 5), (uint32_t)(id / 2U + 1U) << 5);
		sys_clear_bits(PCRM_ANA_CTL, BIT(8) | BIT(11));
		if (id == 1U) {
			sys_write32(0x0060U, AON_PMCTL2_1);
		}
	}

	sys_set_bits(PCRM_ANA_CTL, ANA_CTL_ADC | ANA_CTL_LDO);
	sys_write32(0x1ffU, ADC_INTR_MASK);
}

static void phy62x2_adc_stop(uint8_t mask, bool differential)
{
	sys_write32(0x1ffU, ADC_INTR_MASK);
	sys_write32(0x1ffU, ADC_INTR_CLEAR);
	for (int i = 0; i < 100 && (sys_read32(ADC_INTR_STATUS) & 0x3ffU) != 0U; i++) {
		sys_write32(0x1ffU, ADC_INTR_CLEAR);
	}
	sys_clear_bits(PCRM_ANA_CTL, ANA_CTL_ADC);
	sys_clear_bits(PCRM_CLKHF_CTL1, BIT(13));
	for (uint8_t id = 1U; id <= LAST_CHANNEL; id++) {
		if ((mask & BIT(id)) != 0U) {
			pad_analog(channels[id].pin, false, false);
			if (differential) {
				pad_analog(channels[id].pin_negative, false, false);
			}
		}
	}
	sys_clear_bits(PCRM_ANA_CTL, ANA_CTL_LDO);
	sys_clear_bits(SW_CLK, SW_CLK_ADCC);
}

static int phy62x2_adc_read(const struct device *dev, const struct adc_sequence *sequence)
{
	struct phy62x2_adc_data *data = dev->data;
	uint8_t mask = sequence->channels & 0xffU;
	bool differential = (mask & data->differential) != 0U;
	uint32_t wanted;
	int16_t *buffer = sequence->buffer;
	size_t count = POPCOUNT(mask);
	int64_t timeout;
	int ret = 0;

	if ((sequence->channels & ~(uint32_t)data->configured) != 0U || mask == 0U) {
		return -EINVAL;
	}
	if (sequence->resolution != 12U || sequence->oversampling != 0U) {
		return -ENOTSUP;
	}
	if (sequence->options != NULL && sequence->options->extra_samplings != 0U) {
		return -ENOTSUP;
	}
	/* One differential pair at a time, and not mixed with the single-ended inputs */
	if (differential && (count != 1U || mask != (mask & data->differential))) {
		return -ENOTSUP;
	}
	if (sequence->buffer_size < count * sizeof(int16_t)) {
		return -ENOMEM;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	phy62x2_adc_start(mask, differential);
	wanted = phy62x2_adc_blocks(mask);
	timeout = k_uptime_ticks() + k_us_to_ticks_ceil64(CONVERSION_TIMEOUT_US);
	while ((sys_read32(ADC_INTR_STATUS) & 0x3ffU) != wanted) {
		if (k_uptime_ticks() > timeout) {
			ret = -ETIMEDOUT;
			break;
		}
	}
	if (ret == 0) {
		for (uint8_t id = 1U; id <= LAST_CHANNEL; id++) {
			uint32_t sum = 0U;

			if ((mask & BIT(id)) == 0U) {
				continue;
			}
			for (uint32_t n = 0U; n < ADC_SAMPLES; n++) {
				sum += sys_read32(ADC_BLOCK(id ^ 1U) +
						  (n + ADC_BLOCK_FIRST_WORD) * 4U) & 0xfffU;
			}
			/* Offset binary for a differential input, as the SDK treats it */
			*buffer++ = (int16_t)(sum / ADC_SAMPLES) - (differential ? 2048 : 0);
		}
	}
	phy62x2_adc_stop(mask, differential);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int phy62x2_adc_init(const struct device *dev)
{
	struct phy62x2_adc_data *data = dev->data;

	k_mutex_init(&data->lock);
	return 0;
}

static DEVICE_API(adc, phy62x2_adc_api) = {
	.channel_setup = phy62x2_adc_channel_setup,
	.read = phy62x2_adc_read,
	.ref_internal = VREF_MV,
};

static struct phy62x2_adc_data adc_data;

DEVICE_DT_INST_DEFINE(0, phy62x2_adc_init, NULL, &adc_data, NULL, POST_KERNEL,
		      CONFIG_ADC_INIT_PRIORITY, &phy62x2_adc_api);
