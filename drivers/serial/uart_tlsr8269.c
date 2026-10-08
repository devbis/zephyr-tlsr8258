/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * TLSR826x UART, polled. The 826x UART moves data by DMA in packets: each
 * packet is a 4-byte little-endian length followed by the payload. TX is
 * started by pointing DMA1 at the packet and writing the TX ready bit. RX
 * is a DMA0 buffer; the hardware writes a packet into it and sets the RX
 * ready bit. Register sequence follows the vendor uart_Init(), uart_Send(),
 * uart_RecBuffInit() and DMA_IRQSourceGet() in libdrivers_826x.a.
 *
 * Interrupt-driven and async APIs are not implemented yet.
 */

#define DT_DRV_COMPAT telink_tlsr8269_uart

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>

#define TLSR8269_REG8(addr)  (*(volatile uint8_t *)(addr))
#define TLSR8269_REG16(addr) (*(volatile uint16_t *)(addr))

#define TLSR8269_REG_RST1          TLSR8269_REG8(0x00800061u)
#define TLSR8269_FLD_RST1_UART     BIT(3)

#define TLSR8269_REG_UART_CLK_DIV  TLSR8269_REG16(0x00800094u)
#define TLSR8269_REG_UART_CTRL0    TLSR8269_REG8(0x00800096u)
#define TLSR8269_REG_UART_CTRL0_HI TLSR8269_REG8(0x00800097u)
#define TLSR8269_REG_UART_RX_TO0   TLSR8269_REG8(0x0080009au)
#define TLSR8269_REG_UART_RX_TO1   TLSR8269_REG8(0x0080009bu)
#define TLSR8269_REG_UART_STATUS0  TLSR8269_REG8(0x0080009du)
#define TLSR8269_REG_UART_STATUS1  TLSR8269_REG8(0x0080009eu)

#define TLSR8269_REG_DMA0_ADDR     TLSR8269_REG16(0x00800500u)
#define TLSR8269_REG_DMA0_SIZE     TLSR8269_REG8(0x00800502u)
#define TLSR8269_REG_DMA0_CTRL_HI  TLSR8269_REG8(0x00800503u)
#define TLSR8269_REG_DMA1_ADDR     TLSR8269_REG16(0x00800504u)
#define TLSR8269_REG_DMA1_CTRL_HI  TLSR8269_REG8(0x00800507u)
#define TLSR8269_REG_DMA_TX_RDY0   TLSR8269_REG8(0x00800524u)
#define TLSR8269_REG_DMA_RX_RDY0   TLSR8269_REG8(0x00800526u)

#define TLSR8269_FLD_UART_CTRL0_RX_DMA_EN BIT(4)
#define TLSR8269_FLD_UART_CTRL0_TX_DMA_EN BIT(5)
#define TLSR8269_FLD_UART_STATUS0_RX_ERR  BIT(7)
#define TLSR8269_FLD_UART_STATUS0_CLR_ERR BIT(6)
#define TLSR8269_FLD_UART_STATUS1_TX_DONE BIT(0)
#define TLSR8269_FLD_DMA_UART_TX          BIT(1)
#define TLSR8269_FLD_DMA_RX_UART          BIT(0)
#define TLSR8269_FLD_UART_CLK_DIV_EN      BIT(15)

#define TLSR8269_UART_RX_BUF_SIZE 256u
#define TLSR8269_UART_PKT_HDR     4u
#define TLSR8269_UART_MIN_BWPC    3u

struct tlsr8269_uart_config {
	uint32_t clock_frequency;
	uint32_t current_speed;
	const struct pinctrl_dev_config *pcfg;
};

struct tlsr8269_uart_data {
	uint8_t tx_pkt[TLSR8269_UART_PKT_HDR + 1u] __aligned(4);
	uint8_t rx_buf[TLSR8269_UART_RX_BUF_SIZE] __aligned(16);
	uint16_t rx_pos;
};

static bool tlsr8269_uart_is_prime(uint32_t n)
{
	if (n <= 3u) {
		return true;
	}

	if ((n % 2u) == 0u || (n % 3u) == 0u) {
		return false;
	}

	for (uint32_t i = 5u; i * i < n; i += 6u) {
		if ((n % i) == 0u || (n % (i + 2u)) == 0u) {
			return false;
		}
	}

	return true;
}

static void tlsr8269_uart_cal_div_and_bwpc(uint32_t baudrate, uint32_t clock,
					   uint16_t *divider, uint8_t *bwpc)
{
	uint32_t prime_int = clock / baudrate;
	uint8_t prime_dec = 10u * clock / baudrate - 10u * prime_int;
	uint8_t position_min = 0u;
	uint8_t position_max = 0u;
	uint32_t min = UINT32_MAX;
	uint32_t max = 0u;
	uint32_t d_int[13];
	uint8_t d_dec[13];

	if (tlsr8269_uart_is_prime(prime_int)) {
		prime_int += 1u;
	} else if (prime_dec > 5u) {
		prime_int += 1u;
		if (tlsr8269_uart_is_prime(prime_int)) {
			prime_int -= 1u;
		}
	}

	for (uint8_t i = 3u; i <= 15u; i++) {
		uint32_t d_intdec = (10u * prime_int) / (i + 1u);

		d_dec[i - 3u] = d_intdec - 10u * (d_intdec / 10u);
		d_int[i - 3u] = d_intdec / 10u;
	}

	for (uint8_t i = 0u; i < ARRAY_SIZE(d_int); i++) {
		if (d_dec[i] <= min && d_int[i] != 1u) {
			min = d_dec[i];
			position_min = i;
		}

		if (d_dec[i] >= max) {
			max = d_dec[i];
			position_max = i;
		}
	}

	if (d_dec[position_min] < 5u && d_dec[position_max] >= 5u) {
		if (d_dec[position_min] < (10u - d_dec[position_max])) {
			*bwpc = position_min + 3u;
			*divider = d_int[position_min] - 1u;
		} else {
			*bwpc = position_max + 3u;
			*divider = d_int[position_max];
		}
	} else if (d_dec[position_min] < 5u && d_dec[position_max] < 5u) {
		*bwpc = position_min + 3u;
		*divider = d_int[position_min] - 1u;
	} else {
		*bwpc = position_max + 3u;
		*divider = d_int[position_max];
	}
}


static int tlsr8269_uart_hw_init(const struct tlsr8269_uart_config *config,
				 struct tlsr8269_uart_data *data)
{
	uint16_t divider = 0u;
	uint8_t bwpc = 0u;
	uintptr_t rx_addr = (uintptr_t)data->rx_buf;

	/* The DMA address registers are 16 bits wide. */
	if ((rx_addr >> 16) != 0u) {
		return -EINVAL;
	}

	TLSR8269_REG_RST1 |= TLSR8269_FLD_RST1_UART;
	TLSR8269_REG_RST1 &= (uint8_t)~TLSR8269_FLD_RST1_UART;

	tlsr8269_uart_cal_div_and_bwpc(config->current_speed, config->clock_frequency,
				       &divider, &bwpc);
	if (bwpc < TLSR8269_UART_MIN_BWPC) {
		return -EINVAL;
	}

	TLSR8269_REG_UART_CLK_DIV = divider | TLSR8269_FLD_UART_CLK_DIV_EN;
	TLSR8269_REG_UART_CTRL0 = bwpc | TLSR8269_FLD_UART_CTRL0_RX_DMA_EN |
				  TLSR8269_FLD_UART_CTRL0_TX_DMA_EN;
	TLSR8269_REG_UART_CTRL0_HI = 0u;
	TLSR8269_REG_UART_RX_TO0 = (uint8_t)((bwpc + 1u) * 12u);
	TLSR8269_REG_UART_RX_TO1 = 1u;
	TLSR8269_REG_DMA0_CTRL_HI = 1u;
	TLSR8269_REG_DMA1_CTRL_HI = 0u;

	/* Arm RX DMA: address, then buffer size in 16-byte units. */
	TLSR8269_REG_DMA0_ADDR = (uint16_t)rx_addr;
	TLSR8269_REG_DMA0_SIZE = (uint8_t)(TLSR8269_UART_RX_BUF_SIZE >> 4);

	return 0;
}

static void tlsr8269_uart_poll_out(const struct device *dev, unsigned char c)
{
	struct tlsr8269_uart_data *data = dev->data;

	while ((TLSR8269_REG_UART_STATUS1 & TLSR8269_FLD_UART_STATUS1_TX_DONE) == 0u) {
	}

	data->tx_pkt[0] = 1u;
	data->tx_pkt[1] = 0u;
	data->tx_pkt[2] = 0u;
	data->tx_pkt[3] = 0u;
	data->tx_pkt[TLSR8269_UART_PKT_HDR] = c;

	TLSR8269_REG_DMA1_ADDR = (uint16_t)(uintptr_t)data->tx_pkt;
	TLSR8269_REG_DMA_TX_RDY0 = TLSR8269_FLD_DMA_UART_TX;
}

static int tlsr8269_uart_poll_in(const struct device *dev, unsigned char *c)
{
	struct tlsr8269_uart_data *data = dev->data;
	uint32_t len;

	if ((TLSR8269_REG_DMA_RX_RDY0 & TLSR8269_FLD_DMA_RX_UART) == 0u) {
		return -1;
	}

	len = (uint32_t)data->rx_buf[0] | ((uint32_t)data->rx_buf[1] << 8) |
	      ((uint32_t)data->rx_buf[2] << 16) | ((uint32_t)data->rx_buf[3] << 24);

	if (len > TLSR8269_UART_RX_BUF_SIZE - TLSR8269_UART_PKT_HDR) {
		len = TLSR8269_UART_RX_BUF_SIZE - TLSR8269_UART_PKT_HDR;
	}

	if (data->rx_pos < len) {
		*c = data->rx_buf[TLSR8269_UART_PKT_HDR + data->rx_pos];
		data->rx_pos++;
		return 0;
	}

	/* Packet consumed: clear the ready bit and wait for the next one. */
	data->rx_pos = 0u;
	TLSR8269_REG_DMA_RX_RDY0 = TLSR8269_FLD_DMA_RX_UART;

	return -1;
}

static int tlsr8269_uart_err_check(const struct device *dev)
{
	ARG_UNUSED(dev);

	if ((TLSR8269_REG_UART_STATUS0 & TLSR8269_FLD_UART_STATUS0_RX_ERR) == 0u) {
		return 0;
	}

	TLSR8269_REG_UART_STATUS0 |= TLSR8269_FLD_UART_STATUS0_CLR_ERR;

	return UART_ERROR_FRAMING;
}

static int tlsr8269_uart_init(const struct device *dev)
{
	const struct tlsr8269_uart_config *config = dev->config;
	struct tlsr8269_uart_data *data = dev->data;
	int ret;

	data->rx_pos = 0u;
	memset(data->rx_buf, 0, sizeof(data->rx_buf));

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	return tlsr8269_uart_hw_init(config, data);
}

static DEVICE_API(uart, tlsr8269_uart_driver_api) = {
	.poll_in = tlsr8269_uart_poll_in,
	.poll_out = tlsr8269_uart_poll_out,
	.err_check = tlsr8269_uart_err_check,
};

#define TLSR8269_UART_INIT(n)                                                                     \
	PINCTRL_DT_INST_DEFINE(n);                                                               \
	static struct tlsr8269_uart_data tlsr8269_uart_data_##n;                                 \
	static const struct tlsr8269_uart_config tlsr8269_uart_config_##n = {                    \
		.clock_frequency = DT_PROP(DT_INST_CLOCKS_CTLR(n), clock_frequency),        \
		.current_speed = DT_INST_PROP(n, current_speed),                                 \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
	};                                                                                        \
	DEVICE_DT_INST_DEFINE(n, tlsr8269_uart_init, NULL, &tlsr8269_uart_data_##n,              \
			      &tlsr8269_uart_config_##n, PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, \
			      &tlsr8269_uart_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8269_UART_INIT)
