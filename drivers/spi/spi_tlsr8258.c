/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_spi

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spi_tlsr8258, CONFIG_SPI_LOG_LEVEL);

#include "spi_context.h"

/*
 * Register map of the TLSR8258 SPI master block, from the vendor
 * platform/chip_8258/register.h and drivers/spi.c (spi_master_init,
 * spi_write, spi_read). Offsets are relative to the DT reg base 0x00800008.
 */
#define TLSR8258_SPI_DATA    0x00u
#define TLSR8258_SPI_CTRL    0x01u
#define TLSR8258_SPI_SP      0x02u
#define TLSR8258_SPI_INV_CLK 0x03u

#define TLSR8258_REG_CLK_EN0 (*(volatile uint8_t *)0x00800063u)

#define TLSR8258_FLD_CLK0_SPI_EN      BIT(0)
#define TLSR8258_FLD_SPI_MASTER_EN    BIT(1)
#define TLSR8258_FLD_SPI_DATA_OUT_DIS BIT(2)
#define TLSR8258_FLD_SPI_RD           BIT(3)
#define TLSR8258_FLD_SPI_BUSY         BIT(6)
#define TLSR8258_FLD_SPI_ENABLE       BIT(7)
#define TLSR8258_FLD_SPI_MODE_MASK    GENMASK(1, 0)
#define TLSR8258_FLD_SPI_DIV_MASK     GENMASK(6, 0)

struct tlsr8258_spi_config {
	uintptr_t base;
	uint32_t clock_frequency;
	const struct pinctrl_dev_config *pcfg;
};

struct tlsr8258_spi_data {
	struct spi_context ctx;
};

static inline uint8_t tlsr8258_spi_rd(const struct tlsr8258_spi_config *cfg, uint32_t off)
{
	return *(volatile uint8_t *)(cfg->base + off);
}

static inline void tlsr8258_spi_wr(const struct tlsr8258_spi_config *cfg, uint32_t off,
				   uint8_t value)
{
	*(volatile uint8_t *)(cfg->base + off) = value;
}

static void tlsr8258_spi_wait_idle(const struct tlsr8258_spi_config *cfg)
{
	while ((tlsr8258_spi_rd(cfg, TLSR8258_SPI_CTRL) & TLSR8258_FLD_SPI_BUSY) != 0u) {
	}
}

/*
 * Clock divider: SPI clock = system clock / ((div + 1) * 2). Pick the
 * smallest divider that does not exceed the requested frequency; requests
 * above half the system clock are clamped to the fastest rate.
 */
static int tlsr8258_spi_clk_div(uint32_t clock, uint32_t frequency, uint8_t *div)
{
	uint32_t d;

	if (frequency == 0u) {
		return -EINVAL;
	}

	if (frequency >= clock / 2u) {
		*div = 0u;
		return 0;
	}

	d = DIV_ROUND_UP(clock, 2u * frequency) - 1u;
	if (d > TLSR8258_FLD_SPI_DIV_MASK) {
		return -ENOTSUP;
	}

	*div = (uint8_t)d;
	return 0;
}

static int tlsr8258_spi_configure(const struct device *dev, const struct spi_config *config)
{
	const struct tlsr8258_spi_config *cfg = dev->config;
	uint8_t div;
	uint8_t mode = 0u;
	int ret;

	if ((config->operation & (SPI_HALF_DUPLEX | SPI_MODE_LOOP | SPI_TRANSFER_LSB)) != 0u) {
		return -ENOTSUP;
	}

	if (SPI_WORD_SIZE_GET(config->operation) != 8u) {
		return -ENOTSUP;
	}

	if (SPI_OP_MODE_GET(config->operation) != SPI_OP_MODE_CONTROLLER) {
		return -ENOTSUP;
	}

	if ((config->operation & SPI_LINES_MASK) != SPI_LINES_SINGLE) {
		return -ENOTSUP;
	}

	ret = tlsr8258_spi_clk_div(cfg->clock_frequency, config->frequency, &div);
	if (ret < 0) {
		return ret;
	}

	/* Register value: bit0 = CPOL, bit1 = CPHA (MODE0..MODE3 map to 0..3). */
	if ((config->operation & SPI_MODE_CPOL) != 0u) {
		mode |= BIT(0);
	}
	if ((config->operation & SPI_MODE_CPHA) != 0u) {
		mode |= BIT(1);
	}

	TLSR8258_REG_CLK_EN0 |= TLSR8258_FLD_CLK0_SPI_EN;
	tlsr8258_spi_wr(cfg, TLSR8258_SPI_CTRL,
			tlsr8258_spi_rd(cfg, TLSR8258_SPI_CTRL) | TLSR8258_FLD_SPI_MASTER_EN);
	tlsr8258_spi_wr(cfg, TLSR8258_SPI_SP, TLSR8258_FLD_SPI_ENABLE | div);
	tlsr8258_spi_wr(cfg, TLSR8258_SPI_INV_CLK,
			(tlsr8258_spi_rd(cfg, TLSR8258_SPI_INV_CLK) &
			 (uint8_t)~TLSR8258_FLD_SPI_MODE_MASK) |
				mode);

	return 0;
}

/* Write the TX buffers byte by byte, as the vendor spi_write() does. */
static void tlsr8258_spi_write_bytes(const struct tlsr8258_spi_config *cfg,
				     struct spi_context *ctx)
{
	tlsr8258_spi_wr(cfg, TLSR8258_SPI_CTRL,
			tlsr8258_spi_rd(cfg, TLSR8258_SPI_CTRL) &
				(uint8_t)~(TLSR8258_FLD_SPI_DATA_OUT_DIS | TLSR8258_FLD_SPI_RD));

	while (spi_context_tx_on(ctx)) {
		uint8_t byte = spi_context_tx_buf_on(ctx) ? *(const uint8_t *)ctx->tx_buf : 0u;

		spi_context_update_tx(ctx, 1, 1);
		tlsr8258_spi_wr(cfg, TLSR8258_SPI_DATA, byte);
		tlsr8258_spi_wait_idle(cfg);
	}
}

/*
 * Read the RX buffers, as the vendor spi_read() does: a dummy data read
 * starts the clock, each further read returns the previous byte, and RD is
 * cleared before the final read.
 */
static void tlsr8258_spi_read_bytes(const struct tlsr8258_spi_config *cfg,
				    struct spi_context *ctx)
{
	tlsr8258_spi_wr(cfg, TLSR8258_SPI_CTRL,
			tlsr8258_spi_rd(cfg, TLSR8258_SPI_CTRL) |
				TLSR8258_FLD_SPI_DATA_OUT_DIS | TLSR8258_FLD_SPI_RD);
	(void)tlsr8258_spi_rd(cfg, TLSR8258_SPI_DATA);
	tlsr8258_spi_wait_idle(cfg);

	while (spi_context_rx_on(ctx)) {
		uint8_t byte;

		if (ctx->rx_len == 1u) {
			tlsr8258_spi_wr(cfg, TLSR8258_SPI_CTRL,
					tlsr8258_spi_rd(cfg, TLSR8258_SPI_CTRL) &
						(uint8_t)~TLSR8258_FLD_SPI_RD);
		}

		byte = tlsr8258_spi_rd(cfg, TLSR8258_SPI_DATA);
		tlsr8258_spi_wait_idle(cfg);

		if (spi_context_rx_buf_on(ctx)) {
			*(uint8_t *)ctx->rx_buf = byte;
		}
		spi_context_update_rx(ctx, 1, 1);
	}
}

/*
 * Full duplex: each write clocks one byte out on SDO and the received bits
 * land in the data register, which a read with RD cleared returns without
 * starting another transfer. The longer side sets the number of clocks; TX
 * is padded with 0xff once exhausted and RX bytes past the end are dropped.
 */
static void tlsr8258_spi_transceive_bytes(const struct tlsr8258_spi_config *cfg,
					  struct spi_context *ctx)
{
	tlsr8258_spi_wr(cfg, TLSR8258_SPI_CTRL,
			tlsr8258_spi_rd(cfg, TLSR8258_SPI_CTRL) &
				(uint8_t)~(TLSR8258_FLD_SPI_DATA_OUT_DIS | TLSR8258_FLD_SPI_RD));

	while (spi_context_tx_on(ctx) || spi_context_rx_on(ctx)) {
		uint8_t tx_byte = 0xffu;
		uint8_t rx_byte;

		if (spi_context_tx_on(ctx)) {
			if (spi_context_tx_buf_on(ctx)) {
				tx_byte = *(const uint8_t *)ctx->tx_buf;
			}
			spi_context_update_tx(ctx, 1, 1);
		}

		tlsr8258_spi_wr(cfg, TLSR8258_SPI_DATA, tx_byte);
		tlsr8258_spi_wait_idle(cfg);

		if (spi_context_rx_on(ctx)) {
			rx_byte = tlsr8258_spi_rd(cfg, TLSR8258_SPI_DATA);

			if (spi_context_rx_buf_on(ctx)) {
				*(uint8_t *)ctx->rx_buf = rx_byte;
			}
			spi_context_update_rx(ctx, 1, 1);
		}
	}
}

static int tlsr8258_spi_transceive(const struct device *dev, const struct spi_config *config,
				   const struct spi_buf_set *tx_bufs,
				   const struct spi_buf_set *rx_bufs)
{
	const struct tlsr8258_spi_config *cfg = dev->config;
	struct tlsr8258_spi_data *data = dev->data;
	int ret;

	spi_context_lock(&data->ctx, false, NULL, NULL, config);

	ret = tlsr8258_spi_configure(dev, config);
	if (ret < 0) {
		spi_context_release(&data->ctx, ret);
		return ret;
	}
	data->ctx.config = config;

	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

	spi_context_cs_control(&data->ctx, true);

	if (spi_context_tx_on(&data->ctx) && spi_context_rx_on(&data->ctx)) {
		tlsr8258_spi_transceive_bytes(cfg, &data->ctx);
	} else if (spi_context_tx_on(&data->ctx)) {
		tlsr8258_spi_write_bytes(cfg, &data->ctx);
	} else if (spi_context_rx_on(&data->ctx)) {
		tlsr8258_spi_read_bytes(cfg, &data->ctx);
	}

	spi_context_cs_control(&data->ctx, false);

	spi_context_complete(&data->ctx, dev, 0);
	ret = spi_context_wait_for_completion(&data->ctx);
	spi_context_release(&data->ctx, ret);

	return ret;
}

static int tlsr8258_spi_release(const struct device *dev, const struct spi_config *config)
{
	struct tlsr8258_spi_data *data = dev->data;

	if (!spi_context_configured(&data->ctx, config)) {
		return -EINVAL;
	}

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static int tlsr8258_spi_init(const struct device *dev)
{
	const struct tlsr8258_spi_config *cfg = dev->config;
	struct tlsr8258_spi_data *data = dev->data;
	int ret;

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	ret = spi_context_cs_configure_all(&data->ctx);
	if (ret < 0) {
		return ret;
	}

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static DEVICE_API(spi, tlsr8258_spi_api) = {
	.transceive = tlsr8258_spi_transceive,
	.release = tlsr8258_spi_release,
};

#define TLSR8258_SPI_INIT(inst)                                                                   \
	PINCTRL_DT_INST_DEFINE(inst);                                                             \
                                                                                                  \
	static struct tlsr8258_spi_data tlsr8258_spi_data_##inst = {                              \
		SPI_CONTEXT_INIT_LOCK(tlsr8258_spi_data_##inst, ctx),                             \
		SPI_CONTEXT_INIT_SYNC(tlsr8258_spi_data_##inst, ctx),                             \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(inst), ctx)                           \
	};                                                                                        \
                                                                                                  \
	static const struct tlsr8258_spi_config tlsr8258_spi_cfg_##inst = {                       \
		.base = DT_INST_REG_ADDR(inst),                                                   \
		.clock_frequency = DT_INST_PROP(inst, clock_frequency),                           \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                     \
	};                                                                                        \
                                                                                                  \
	SPI_DEVICE_DT_INST_DEFINE(inst, tlsr8258_spi_init, NULL, &tlsr8258_spi_data_##inst,       \
				  &tlsr8258_spi_cfg_##inst, POST_KERNEL,                          \
				  CONFIG_SPI_INIT_PRIORITY, &tlsr8258_spi_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_SPI_INIT)
