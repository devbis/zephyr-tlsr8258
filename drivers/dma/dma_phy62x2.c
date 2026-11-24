/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT phyplus_phy62x2_dma

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/*
 * A DesignWare AHB DMA controller with four channels, as the PHY62XX SDK 3.1.1 drives it. The
 * handshake interfaces are in the classic fields of CFG_H, bits 10:7 for the source and 14:11 for
 * the destination, which the DesignWare driver of Zephyr, made for the Intel layout, does not set.
 */
#define DMA_BASE DT_INST_REG_ADDR(0)
#define CH_STRIDE 0x58U
#define CH_SAR(n) (DMA_BASE + CH_STRIDE * (n) + 0x00U)
#define CH_DAR(n) (DMA_BASE + CH_STRIDE * (n) + 0x08U)
#define CH_LLP(n) (DMA_BASE + CH_STRIDE * (n) + 0x10U)
#define CH_CTL_L(n) (DMA_BASE + CH_STRIDE * (n) + 0x18U)
#define CH_CTL_H(n) (DMA_BASE + CH_STRIDE * (n) + 0x1cU)
#define CH_CFG_L(n) (DMA_BASE + CH_STRIDE * (n) + 0x40U)
#define CH_CFG_H(n) (DMA_BASE + CH_STRIDE * (n) + 0x44U)
#define RAW_TFR (DMA_BASE + 0x2c0U)
#define STATUS_TFR (DMA_BASE + 0x2e8U)
#define STATUS_ERR (DMA_BASE + 0x308U)
#define MASK_TFR (DMA_BASE + 0x310U)
#define MASK_ERR (DMA_BASE + 0x330U)
#define CLEAR_TFR (DMA_BASE + 0x338U)
#define CLEAR_ERR (DMA_BASE + 0x358U)
#define DMA_CFG (DMA_BASE + 0x398U)
#define CH_EN (DMA_BASE + 0x3a0U)

#define NUM_CHANNELS 4U
/* The upper half of the interrupt mask and of the channel enable registers writes the lower half */
#define WRITE_ENABLE(n) BIT(8U + (n))

#define CTL_INT_EN BIT(0)
#define CTL_TT_FC_SHIFT 20U
#define CFG_H_SRC_PER_SHIFT 7U
#define CFG_H_DST_PER_SHIFT 11U

/* The channel 0 has a block of 0x7ff transfers at most, the others 0x1f */
#define MAX_BLOCK_TS(n) ((n) == 0U ? 0x7ffU : 0x1fU)

struct phy62x2_dma_channel {
	dma_callback_t callback;
	void *user_data;
	bool configured;
};

struct phy62x2_dma_data {
	struct phy62x2_dma_channel channels[NUM_CHANNELS];
};

static int width_code(uint32_t bytes)
{
	switch (bytes) {
	case 1U:
		return 0;
	case 2U:
		return 1;
	case 4U:
		return 2;
	default:
		return -EINVAL;
	}
}

/* The burst length in data items to the code of the register: 1 is 0, 4 is 1, and so on up to 256 */
static int burst_code(uint32_t length)
{
	if (length == 0U || length == 1U) {
		return 0;
	}
	if (length < 4U || length > 256U || !is_power_of_two(length)) {
		return -EINVAL;
	}
	return (int)LOG2(length) - 1;
}

static int phy62x2_dma_config(const struct device *dev, uint32_t channel,
			      struct dma_config *config)
{
	struct phy62x2_dma_data *data = dev->data;
	const struct dma_block_config *block = config->head_block;
	uint32_t tt_fc;
	uint32_t cfg_h = 0U;
	uint32_t ctl;
	int src_width;
	int dst_width;
	int src_burst;
	int dst_burst;
	uint32_t block_ts;
	unsigned int key;

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}
	if (config->block_count != 1U || block == NULL || block->next_block != NULL) {
		return -ENOTSUP;
	}
	if (config->channel_direction != MEMORY_TO_MEMORY &&
	    config->channel_direction != MEMORY_TO_PERIPHERAL &&
	    config->channel_direction != PERIPHERAL_TO_MEMORY) {
		return -ENOTSUP;
	}
	if (block->source_addr_adj > DMA_ADDR_ADJ_NO_CHANGE ||
	    block->dest_addr_adj > DMA_ADDR_ADJ_NO_CHANGE) {
		return -ENOTSUP;
	}

	src_width = width_code(config->source_data_size);
	dst_width = width_code(config->dest_data_size);
	src_burst = burst_code(config->source_burst_length);
	dst_burst = burst_code(config->dest_burst_length);
	if (src_width < 0 || dst_width < 0 || src_burst < 0 || dst_burst < 0) {
		return -EINVAL;
	}
	if (config->dma_slot > 15U) {
		return -EINVAL;
	}

	/* The block is counted in transfers of the source width */
	block_ts = block->block_size / config->source_data_size;
	if (block_ts == 0U || block_ts > MAX_BLOCK_TS(channel) ||
	    (block->block_size % config->source_data_size) != 0U) {
		return -EINVAL;
	}

	if (config->channel_direction == MEMORY_TO_PERIPHERAL) {
		tt_fc = 1U;
		cfg_h = config->dma_slot << CFG_H_DST_PER_SHIFT;
	} else if (config->channel_direction == PERIPHERAL_TO_MEMORY) {
		tt_fc = 2U;
		cfg_h = config->dma_slot << CFG_H_SRC_PER_SHIFT;
	} else {
		tt_fc = 0U;
	}

	ctl = (tt_fc << CTL_TT_FC_SHIFT) | ((uint32_t)src_burst << 14) | ((uint32_t)dst_burst << 11) |
	      ((uint32_t)src_width << 4) | ((uint32_t)dst_width << 1) |
	      ((uint32_t)block->source_addr_adj << 9) | ((uint32_t)block->dest_addr_adj << 7) |
	      CTL_INT_EN;

	key = irq_lock();
	if ((sys_read32(CH_EN) & BIT(channel)) != 0U) {
		irq_unlock(key);
		return -EBUSY;
	}

	/* The order of hal_dma_config_channel() of the SDK */
	sys_write32(BIT(channel), CLEAR_TFR);
	sys_write32(BIT(channel), CLEAR_ERR);
	sys_write32(WRITE_ENABLE(channel), MASK_TFR);
	sys_write32(WRITE_ENABLE(channel), MASK_ERR);
	sys_write32(block->source_address, CH_SAR(channel));
	sys_write32(block->dest_address, CH_DAR(channel));
	sys_write32(0U, CH_LLP(channel));
	sys_write32(block_ts, CH_CTL_H(channel));
	sys_write32(cfg_h, CH_CFG_H(channel));
	sys_write32(0U, CH_CFG_L(channel));
	sys_write32(ctl, CH_CTL_L(channel));

	data->channels[channel].callback = config->dma_callback;
	data->channels[channel].user_data = config->user_data;
	data->channels[channel].configured = true;
	if (config->dma_callback != NULL) {
		sys_write32(WRITE_ENABLE(channel) | BIT(channel), MASK_TFR);
		if (config->error_callback_dis == 0U) {
			sys_write32(WRITE_ENABLE(channel) | BIT(channel), MASK_ERR);
		}
	}
	irq_unlock(key);

	return 0;
}

static int phy62x2_dma_start(const struct device *dev, uint32_t channel)
{
	struct phy62x2_dma_data *data = dev->data;

	if (channel >= NUM_CHANNELS || !data->channels[channel].configured) {
		return -EINVAL;
	}

	sys_write32(WRITE_ENABLE(channel) | BIT(channel), CH_EN);

	return 0;
}

static int phy62x2_dma_stop(const struct device *dev, uint32_t channel)
{
	ARG_UNUSED(dev);

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}

	/* As hal_dma_stop_channel(): clear the flag, then disable the channel */
	sys_write32(BIT(channel), CLEAR_TFR);
	sys_write32(WRITE_ENABLE(channel), CH_EN);

	return 0;
}

static int phy62x2_dma_reload(const struct device *dev, uint32_t channel, uint32_t src,
			      uint32_t dst, size_t size)
{
	ARG_UNUSED(dev);

	if (channel >= NUM_CHANNELS || (sys_read32(CH_EN) & BIT(channel)) != 0U) {
		return channel >= NUM_CHANNELS ? -EINVAL : -EBUSY;
	}

	/* The width of the source is in CTL_L, bits 6:4 */
	size >>= (sys_read32(CH_CTL_L(channel)) >> 4) & 0x7U;
	if (size == 0U || size > MAX_BLOCK_TS(channel)) {
		return -EINVAL;
	}

	sys_write32(src, CH_SAR(channel));
	sys_write32(dst, CH_DAR(channel));
	sys_write32((uint32_t)size, CH_CTL_H(channel));

	return 0;
}

static int phy62x2_dma_get_status(const struct device *dev, uint32_t channel,
				  struct dma_status *status)
{
	ARG_UNUSED(dev);

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}

	status->busy = (sys_read32(CH_EN) & BIT(channel)) != 0U;
	status->pending_length = 0U;

	return 0;
}

static void phy62x2_dma_isr(const struct device *dev)
{
	struct phy62x2_dma_data *data = dev->data;
	const uint32_t done = sys_read32(STATUS_TFR);
	const uint32_t error = sys_read32(STATUS_ERR);

	for (uint32_t ch = 0U; ch < NUM_CHANNELS; ch++) {
		const struct phy62x2_dma_channel *chan = &data->channels[ch];
		int status = 0;

		if ((done & BIT(ch)) == 0U && (error & BIT(ch)) == 0U) {
			continue;
		}
		if ((error & BIT(ch)) != 0U) {
			status = -EIO;
			sys_write32(BIT(ch), CLEAR_ERR);
		}
		/* The channel is disabled and the flag cleared, as in the handler of the SDK */
		phy62x2_dma_stop(dev, ch);
		if (chan->callback != NULL) {
			chan->callback(dev, chan->user_data, ch, status);
		}
	}
}

static int phy62x2_dma_init(const struct device *dev)
{
	/* The clock and the reset come from the SoC start-up code */
	sys_write32(1U, DMA_CFG);
	sys_write32(WRITE_ENABLE(0) | WRITE_ENABLE(1) | WRITE_ENABLE(2) | WRITE_ENABLE(3), MASK_TFR);
	sys_write32(WRITE_ENABLE(0) | WRITE_ENABLE(1) | WRITE_ENABLE(2) | WRITE_ENABLE(3), MASK_ERR);
	sys_write32(0x0fU, CLEAR_TFR);
	sys_write32(0x0fU, CLEAR_ERR);

	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), phy62x2_dma_isr,
		    DEVICE_DT_INST_GET(0), 0);
	irq_enable(DT_INST_IRQN(0));

	return 0;
}

static DEVICE_API(dma, phy62x2_dma_api) = {
	.config = phy62x2_dma_config,
	.reload = phy62x2_dma_reload,
	.start = phy62x2_dma_start,
	.stop = phy62x2_dma_stop,
	.get_status = phy62x2_dma_get_status,
};

static struct phy62x2_dma_data dma_data;

DEVICE_DT_INST_DEFINE(0, phy62x2_dma_init, NULL, &dma_data, NULL, POST_KERNEL,
		      CONFIG_DMA_INIT_PRIORITY, &phy62x2_dma_api);
