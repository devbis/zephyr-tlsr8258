/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Telink TLSR8258 SAR ADC, following the vendor adc.c / drv_adc.c sequence.
 *
 * The converter has one miscellaneous channel that samples one GPIO of
 * PB0..PB7 or PC4..PC5 (zephyr,input-positive 1..10, in vendor pin order).
 * Each channel can run in one of two modes:
 *  - base mode: the pin is an input, measured against GND;
 *  - VBAT mode (telink,vbat-mode): the pin is driven high and measured the
 *    same way, as the vendor adc_vbat_init() does.
 * Both modes use the differential input path with GND as the negative input,
 * as the vendor does. Results are reported in millivolts, scaled by the
 * vendor pre-scaler, in an int16_t buffer. The resolution field of the
 * sequence is ignored.
 *
 * Only one channel is active at a time: channel_setup() reprograms the shared
 * converter.
 */

#define DT_DRV_COMPAT telink_tlsr8258_adc

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_tlsr8258.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(adc_tlsr8258, CONFIG_ADC_LOG_LEVEL);

#define TLSR8258_REG8(addr)  (*(volatile uint8_t *)(addr))
#define TLSR8258_REG16(addr) (*(volatile uint16_t *)(addr))

/* Digital registers. */
#define TLSR8258_REG_RST1         TLSR8258_REG8(0x00800061u)
#define TLSR8258_FLD_RST1_ADC     BIT(5)
#define TLSR8258_REG_DFIFO_MODE   TLSR8258_REG8(0x00800b10u)
#define TLSR8258_FLD_DFIFO2_IN    BIT(2)
#define TLSR8258_REG_DFIFO2_ADDR  TLSR8258_REG16(0x00800b08u)
#define TLSR8258_REG_DFIFO2_SIZE  TLSR8258_REG8(0x00800b0au)
#define TLSR8258_REG_DFIFO2_WPTR  TLSR8258_REG16(0x00800b1eu)

/* GPIO registers, one block of eight per port group. */
#define TLSR8258_REG_GPIO_IE(grp)   TLSR8258_REG8(0x00800581u + ((grp) << 3))
#define TLSR8258_REG_GPIO_OEN(grp)  TLSR8258_REG8(0x00800582u + ((grp) << 3))
#define TLSR8258_REG_GPIO_OUT(grp)  TLSR8258_REG8(0x00800583u + ((grp) << 3))
#define TLSR8258_REG_GPIO_FUNC(grp) TLSR8258_REG8(0x00800586u + ((grp) << 3))
#define TLSR8258_GPIO_ANA_IE_PB     0xbdu
#define TLSR8258_GPIO_ANA_IE_PC     0xc0u

/* Analog registers (AFE space). */
#define TLSR8258_REG_ANA_ADDR TLSR8258_REG8(0x008000b8u)
#define TLSR8258_REG_ANA_DATA TLSR8258_REG8(0x008000b9u)
#define TLSR8258_REG_ANA_CTRL TLSR8258_REG8(0x008000bau)
#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)

#define TLSR8258_ANA_CLK_SETTING   0x82u
#define TLSR8258_FLD_CLK_24M_SAR   BIT(6)
#define TLSR8258_ANA_SAMPLE_CLK    0xf4u
#define TLSR8258_ANA_VREF          0xe7u
#define TLSR8258_ANA_VBAT_DIV      0xf9u
#define TLSR8258_FLD_VBAT_DIV      (BIT(2) | BIT(3))
#define TLSR8258_FLD_PRESCALE_EN   BIT(4)
#define TLSR8258_ANA_AIN_MISC      0xe8u
#define TLSR8258_ANA_RES_MISC      0xecu
#define TLSR8258_FLD_RES_MISC      (BIT(0) | BIT(1))
#define TLSR8258_FLD_DIFF_MISC     BIT(6)
#define TLSR8258_ANA_TSAMPLE_MISC  0xeeu
#define TLSR8258_ANA_R_MAX_MC      0xefu
#define TLSR8258_ANA_R_MAX_C       0xf0u
#define TLSR8258_ANA_R_MAX_S       0xf1u
#define TLSR8258_ANA_CHN_EN        0xf2u
#define TLSR8258_FLD_CHN_EN_MISC   BIT(2)
#define TLSR8258_ANA_AIN_SCALE     0xfau
#define TLSR8258_FLD_SEL_AIN_SCALE (BIT(6) | BIT(7))
#define TLSR8258_ANA_PGA_CTRL      0xfcu
#define TLSR8258_FLD_GAIN_L        (BIT(0) | BIT(1))
#define TLSR8258_FLD_GAIN_R        (BIT(2) | BIT(3))
#define TLSR8258_FLD_ADC_MODE      BIT(4)
#define TLSR8258_FLD_SAR_PD        BIT(5)
#define TLSR8258_FLD_PGA_PD_L      BIT(6)
#define TLSR8258_FLD_PGA_PD_R      BIT(7)

/* Vendor enum values used by the sequence. */
#define TLSR8258_VREF_1P2V        2u
#define TLSR8258_PRESCALER_1F8    3u
#define TLSR8258_RES14            3u
#define TLSR8258_TSAMPLE_6_CYC    1u
#define TLSR8258_GAIN_BIAS_100    1u
#define TLSR8258_GND_INPUT        15u
#define TLSR8258_SAMPLE_CLK_DIV   5u
#define TLSR8258_SAMPLE_NUM       8u
#define TLSR8258_SAMPLE_WAIT_US   90u /* two periods at 23 kHz */
#define TLSR8258_VREF_DEFAULT_MV  1175u
#define TLSR8258_PIN_COUNT        10u

struct adc_tlsr8258_config {
	/* Bit n set when channel n runs in VBAT mode. */
	uint32_t vbat_mask;
};

struct adc_tlsr8258_data {
	struct k_mutex lock;
	uint16_t vref_mv;
	int8_t vref_offset_mv;
	uint8_t pre_scale;
	bool configured;
	uint8_t channel_id;
	uint8_t pin_index;
	bool vbat;
};

static void tlsr8258_analog_wait(void)
{
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr8258_analog_read(uint8_t addr)
{
	uint8_t value;
	unsigned int key = irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	tlsr8258_analog_wait();
	value = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);

	return value;
}

static void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	tlsr8258_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);
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

/* Vendor adc_init(): bring the converter to a known state, powered down. */
static void tlsr8258_adc_hw_init(void)
{
	/* Power the SAR ADC down while it is being configured. */
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, 0u, TLSR8258_FLD_SAR_PD);
	tlsr8258_adc_reset_module();

	tlsr8258_analog_update(TLSR8258_ANA_CLK_SETTING, 0u, TLSR8258_FLD_CLK_24M_SAR);
	tlsr8258_analog_write(TLSR8258_ANA_SAMPLE_CLK, TLSR8258_SAMPLE_CLK_DIV & 0x07u);

	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, 0u, TLSR8258_FLD_PGA_PD_L | TLSR8258_FLD_PGA_PD_R);

	/* Gain-stage bias: 100 % for both PGA channels. */
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, TLSR8258_FLD_GAIN_L | TLSR8258_FLD_GAIN_R,
			       TLSR8258_GAIN_BIAS_100 | (TLSR8258_GAIN_BIAS_100 << 2));

	/* Keep the miscellaneous channel's data FIFO off until sampling. */
	TLSR8258_REG_DFIFO_MODE &= (uint8_t)~TLSR8258_FLD_DFIFO2_IN;

	/* 23 kHz rate: capture state 1023, set state 15. */
	tlsr8258_analog_write(TLSR8258_ANA_R_MAX_MC, 0xffu);
	tlsr8258_analog_write(TLSR8258_ANA_R_MAX_C, 0x00u);
	tlsr8258_analog_write(TLSR8258_ANA_R_MAX_S, (0x03u << 6) | 0x0fu);
}

static void tlsr8258_adc_power(bool on)
{
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, TLSR8258_FLD_SAR_PD, on ? 0u : TLSR8258_FLD_SAR_PD);
}

/* Map the vendor pin index (1..10 = PB0..PB7, PC4, PC5) to group and bit. */
static void tlsr8258_adc_pin_info(uint8_t pin_index, uint8_t *group, uint8_t *bit, bool *is_pb)
{
	if (pin_index <= 8u) {
		*group = 1u;
		*bit = BIT(pin_index - 1u);
		*is_pb = true;
	} else {
		*group = 2u;
		*bit = BIT(pin_index - 9u + 4u);
		*is_pb = false;
	}
}

static void tlsr8258_adc_pin_setup(uint8_t pin_index, bool vbat)
{
	uint8_t group;
	uint8_t bit;
	bool is_pb;

	tlsr8258_adc_pin_info(pin_index, &group, &bit, &is_pb);

	/* GPIO function, input buffer off. */
	TLSR8258_REG_GPIO_FUNC(group) |= bit;
	tlsr8258_analog_update(is_pb ? TLSR8258_GPIO_ANA_IE_PB : TLSR8258_GPIO_ANA_IE_PC, bit, 0u);

	if (vbat) {
		/* Drive the pin high: the vendor VBAT mode output. */
		TLSR8258_REG_GPIO_OEN(group) &= (uint8_t)~bit;
		TLSR8258_REG_GPIO_OUT(group) |= bit;
	} else {
		TLSR8258_REG_GPIO_OEN(group) |= bit;
		TLSR8258_REG_GPIO_OUT(group) &= (uint8_t)~bit;
	}
}

static void tlsr8258_adc_configure(uint8_t pin_index, uint8_t *pre_scale)
{
	/* Mode and input: pin against GND, differential path. */
	tlsr8258_analog_write(TLSR8258_ANA_AIN_MISC, TLSR8258_GND_INPUT | ((uint8_t)pin_index << 4));
	tlsr8258_analog_update(TLSR8258_ANA_RES_MISC, 0u, TLSR8258_FLD_DIFF_MISC);

	/* Only the miscellaneous channel, with two max-state cycles. */
	tlsr8258_analog_write(TLSR8258_ANA_CHN_EN, TLSR8258_FLD_CHN_EN_MISC | (2u << 4));

	/* Divider from the battery input is off in both modes. */
	tlsr8258_analog_update(TLSR8258_ANA_VBAT_DIV, TLSR8258_FLD_VBAT_DIV, 0u);

	/* Reference: 1.2 V internal, VBAT mode uses the same reference. */
	tlsr8258_analog_write(TLSR8258_ANA_VREF, (TLSR8258_VREF_1P2V) | (TLSR8258_VREF_1P2V << 2) |
						    (TLSR8258_VREF_1P2V << 4));
	tlsr8258_analog_write(TLSR8258_ANA_AIN_SCALE,
			      (tlsr8258_analog_read(TLSR8258_ANA_AIN_SCALE) & 0xc0u) | 0x3du);

	tlsr8258_analog_update(TLSR8258_ANA_RES_MISC, TLSR8258_FLD_RES_MISC, TLSR8258_RES14);
	tlsr8258_analog_write(TLSR8258_ANA_TSAMPLE_MISC, TLSR8258_TSAMPLE_6_CYC);

	/* Pre-scaler 1/8 and the scaler enable bit. */
	tlsr8258_analog_update(TLSR8258_ANA_AIN_SCALE, TLSR8258_FLD_SEL_AIN_SCALE,
			       (uint8_t)(TLSR8258_PRESCALER_1F8 << 6));
	tlsr8258_analog_update(TLSR8258_ANA_VBAT_DIV, 0u, TLSR8258_FLD_PRESCALE_EN);
	*pre_scale = (uint8_t)BIT(TLSR8258_PRESCALER_1F8);

	/* Normal (not RNS) mode. */
	tlsr8258_analog_update(TLSR8258_ANA_PGA_CTRL, TLSR8258_FLD_ADC_MODE, 0u);
}

static int adc_tlsr8258_channel_setup(const struct device *dev,
				      const struct adc_channel_cfg *channel_cfg)
{
	const struct adc_tlsr8258_config *config = dev->config;
	struct adc_tlsr8258_data *data = dev->data;
	bool vbat;

	if (channel_cfg->channel_id >= 32u) {
		return -EINVAL;
	}
	if (channel_cfg->reference != ADC_REF_INTERNAL) {
		LOG_ERR("only the internal reference is supported");
		return -ENOTSUP;
	}
	if ((channel_cfg->gain != ADC_GAIN_1) || channel_cfg->differential) {
		LOG_ERR("only single-ended gain 1 is supported");
		return -ENOTSUP;
	}
	if ((channel_cfg->input_positive < 1u) || (channel_cfg->input_positive > TLSR8258_PIN_COUNT)) {
		LOG_ERR("input %u is not an ADC pin", channel_cfg->input_positive);
		return -EINVAL;
	}

	vbat = (config->vbat_mask & BIT(channel_cfg->channel_id)) != 0u;

	k_mutex_lock(&data->lock, K_FOREVER);
	tlsr8258_adc_hw_init();
	tlsr8258_adc_pin_setup((uint8_t)channel_cfg->input_positive, vbat);
	tlsr8258_adc_configure((uint8_t)channel_cfg->input_positive, &data->pre_scale);
	tlsr8258_adc_power(true);

	data->channel_id = channel_cfg->channel_id;
	data->pin_index = (uint8_t)channel_cfg->input_positive;
	data->vbat = vbat;
	data->configured = true;
	k_mutex_unlock(&data->lock);

	return 0;
}

/* Vendor adc_sample_and_get_result_op(): sample, sort, average the middle half. */
static int adc_tlsr8258_sample_mv(struct adc_tlsr8258_data *data)
{
	volatile int32_t dfifo[TLSR8258_SAMPLE_NUM] __aligned(16);
	uint16_t sample[TLSR8258_SAMPLE_NUM];
	uint32_t average;
	uint32_t code;
	uint32_t mv;

	tlsr8258_adc_reset_module();
	for (size_t i = 0; i < TLSR8258_SAMPLE_NUM; i++) {
		dfifo[i] = 0;
	}

	/* Point the misc-channel FIFO at the buffer and clear its write pointer. */
	TLSR8258_REG_DFIFO2_ADDR = (uint16_t)(uintptr_t)dfifo;
	TLSR8258_REG_DFIFO2_SIZE = (uint8_t)((sizeof(dfifo) >> 4) - 1u);
	TLSR8258_REG_DFIFO2_WPTR = 0u;
	TLSR8258_REG_DFIFO_MODE |= TLSR8258_FLD_DFIFO2_IN;

	k_busy_wait(TLSR8258_SAMPLE_WAIT_US);
	for (size_t i = 0; i < TLSR8258_SAMPLE_NUM; i++) {
		k_busy_wait(TLSR8258_SAMPLE_WAIT_US);
		/* BIT(13) is the sign of a differential result: negative reads as 0. */
		if ((dfifo[i] & BIT(13)) != 0) {
			sample[i] = 0u;
		} else {
			sample[i] = (uint16_t)(dfifo[i] & 0x1fff);
		}
	}
	TLSR8258_REG_DFIFO_MODE &= (uint8_t)~TLSR8258_FLD_DFIFO2_IN;

	/* Insertion sort, then average the middle four samples. */
	for (size_t i = 1; i < TLSR8258_SAMPLE_NUM; i++) {
		uint16_t temp = sample[i];
		size_t j = i;

		while ((j > 0u) && (sample[j - 1u] > temp)) {
			sample[j] = sample[j - 1u];
			j--;
		}
		sample[j] = temp;
	}
	average = ((uint32_t)sample[2] + sample[3] + sample[4] + sample[5]) / 4u;
	code = average;

	if (code == 0u) {
		return 0;
	}

	/* code * pre_scale * Vref / 2^13 + offset, as in the vendor code. */
	mv = ((code * data->pre_scale * data->vref_mv) >> 13) + data->vref_offset_mv;
	return (int)mv;
}

static int adc_tlsr8258_read(const struct device *dev, const struct adc_sequence *sequence)
{
	struct adc_tlsr8258_data *data = dev->data;
	int16_t mv;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->configured) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}
	if (sequence->channels != BIT(data->channel_id)) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}
	if (sequence->buffer_size < sizeof(int16_t)) {
		k_mutex_unlock(&data->lock);
		return -ENOMEM;
	}

	ret = adc_tlsr8258_sample_mv(data);
	k_mutex_unlock(&data->lock);

	if (ret < 0) {
		return ret;
	}

	mv = (int16_t)MIN(ret, INT16_MAX);
	memcpy(sequence->buffer, &mv, sizeof(mv));
	return 0;
}

int adc_tlsr8258_set_calibration(const struct device *dev, uint16_t vref_mv, int8_t offset_mv)
{
	struct adc_tlsr8258_data *data = dev->data;

	if (vref_mv == 0u) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->vref_mv = vref_mv;
	data->vref_offset_mv = offset_mv;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int adc_tlsr8258_init(const struct device *dev)
{
	struct adc_tlsr8258_data *data = dev->data;

	k_mutex_init(&data->lock);
	data->vref_mv = TLSR8258_VREF_DEFAULT_MV;
	data->vref_offset_mv = 0;
	data->pre_scale = 1u;
	data->configured = false;

	return 0;
}

static DEVICE_API(adc, adc_tlsr8258_api) = {
	.channel_setup = adc_tlsr8258_channel_setup,
	.read = adc_tlsr8258_read,
	/* Nominal internal reference; samples are already scaled to millivolts. */
	.ref_internal = 1200,
};

#define TLSR8258_ADC_VBAT_BIT(node_id) \
	| (DT_PROP(node_id, telink_vbat_mode) ? BIT(DT_REG_ADDR(node_id)) : 0u)

#define TLSR8258_ADC_INIT(n)                                                                       \
	static const struct adc_tlsr8258_config adc_tlsr8258_config_##n = {                        \
		.vbat_mask = 0u DT_INST_FOREACH_CHILD(n, TLSR8258_ADC_VBAT_BIT),                   \
	};                                                                                         \
	static struct adc_tlsr8258_data adc_tlsr8258_data_##n;                                     \
	DEVICE_DT_INST_DEFINE(n, adc_tlsr8258_init, NULL, &adc_tlsr8258_data_##n,                  \
			      &adc_tlsr8258_config_##n, POST_KERNEL, CONFIG_ADC_INIT_PRIORITY,     \
			      &adc_tlsr8258_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_ADC_INIT)
