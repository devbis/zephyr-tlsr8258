/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_uart

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#define TLSR8258_REG8(addr)  (*(volatile uint8_t *)(addr))
#define TLSR8258_REG16(addr) (*(volatile uint16_t *)(addr))

#define TLSR8258_REG_RST0    TLSR8258_REG8(0x00800060u)
#define TLSR8258_REG_CLK_EN0 TLSR8258_REG8(0x00800063u)

#define TLSR8258_REG_UART_DATA_BUF(n) TLSR8258_REG8(0x00800090u + (n))
#define TLSR8258_REG_UART_CLK_DIV     TLSR8258_REG16(0x00800094u)
#define TLSR8258_REG_UART_CTRL0       TLSR8258_REG8(0x00800096u)
#define TLSR8258_REG_UART_CTRL1       TLSR8258_REG8(0x00800097u)
#define TLSR8258_REG_UART_RX_TIMEOUT0 TLSR8258_REG8(0x0080009au)
#define TLSR8258_REG_UART_RX_TIMEOUT1 TLSR8258_REG8(0x0080009bu)
#define TLSR8258_REG_UART_BUF_CNT     TLSR8258_REG8(0x0080009cu)
#define TLSR8258_REG_UART_STATUS0     TLSR8258_REG8(0x0080009du)

#define TLSR8258_REG_GPIO_IE(pin)   TLSR8258_REG8(0x00800581u + (((pin) >> 8) << 3))
#define TLSR8258_REG_GPIO_FUNC(pin) TLSR8258_REG8(0x00800586u + (((pin) >> 8) << 3))
#define TLSR8258_REG_MUX_A1         TLSR8258_REG8(0x008005a8u)

#define TLSR8258_REG_ANA_ADDR TLSR8258_REG8(0x008000b8u)
#define TLSR8258_REG_ANA_DATA TLSR8258_REG8(0x008000b9u)
#define TLSR8258_REG_ANA_CTRL TLSR8258_REG8(0x008000bau)

#define FLD_RST0_UART        BIT(2)
#define FLD_CLK0_UART_EN     BIT(2)
#define FLD_UART_CLK_DIV_EN  BIT(15)
#define FLD_UART_BPWC        GENMASK(3, 0)
#define FLD_UART_TIMEOUT_MUL GENMASK(1, 0)
#define FLD_UART_PARITY_EN   BIT(2)
#define FLD_UART_STOP_BIT    GENMASK(5, 4)
#define FLD_UART_RX_BUF_CNT  GENMASK(3, 0)
#define FLD_UART_TX_BUF_CNT  GENMASK(7, 4)
#define FLD_UART_RX_ERR_FLAG BIT(7)
#define FLD_ANA_BUSY         BIT(0)
#define FLD_ANA_RW           BIT(5)
#define FLD_ANA_CYC0         BIT(6)

#define GPIO_PA0 BIT(0)
#define GPIO_PA2 BIT(2)
#define UART_TX_BUF_CNT_MAX 8u
#define UART_DATA_BUF_COUNT 4u
#define PM_PIN_PULLUP_10K 3u

struct tlsr8258_uart_config {
	uint32_t clock_frequency;
	uint32_t current_speed;
	uint32_t tx_pin;
	uint32_t rx_pin;
#ifdef CONFIG_PINCTRL
	const struct pinctrl_dev_config *pcfg;
#endif
};

struct tlsr8258_uart_data {
	uint8_t tx_index;
	uint8_t rx_index;
};

static void tlsr8258_analog_wait(void)
{
	while ((TLSR8258_REG_ANA_CTRL & FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr8258_analog_read(uint8_t addr)
{
	uint32_t key = irq_lock();
	uint8_t data;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = FLD_ANA_CYC0;
	tlsr8258_analog_wait();
	data = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);

	return data;
}

static void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	uint32_t key = irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = FLD_ANA_CYC0 | FLD_ANA_RW;
	tlsr8258_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
	irq_unlock(key);
}

static bool tlsr8258_uart_is_prime(uint32_t n)
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

static void tlsr8258_uart_cal_div_and_bwpc(uint32_t baudrate, uint32_t clock,
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

	if (tlsr8258_uart_is_prime(prime_int)) {
		prime_int += 1u;
	} else if (prime_dec > 5u) {
		prime_int += 1u;
		if (tlsr8258_uart_is_prime(prime_int)) {
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

static uint8_t tlsr8258_uart_tx_count(void)
{
	return (TLSR8258_REG_UART_BUF_CNT & FLD_UART_TX_BUF_CNT) >> 4;
}

static uint8_t tlsr8258_uart_rx_count(void)
{
	return TLSR8258_REG_UART_BUF_CNT & FLD_UART_RX_BUF_CNT;
}

static void tlsr8258_uart_pullup(uint32_t pin)
{
	uint8_t base = 0x0eu + ((pin >> 8) << 1) + ((pin & 0xf0u) ? 1u : 0u);
	uint8_t shift;
	uint8_t mask;
	uint8_t value;

	if ((pin & 0x11u) != 0u) {
		shift = 0u;
		mask = 0xfcu;
	} else if ((pin & 0x22u) != 0u) {
		shift = 2u;
		mask = 0xf3u;
	} else if ((pin & 0x44u) != 0u) {
		shift = 4u;
		mask = 0xcfu;
	} else if ((pin & 0x88u) != 0u) {
		shift = 6u;
		mask = 0x3fu;
	} else {
		return;
	}

	value = (tlsr8258_analog_read(base) & mask) | (PM_PIN_PULLUP_10K << shift);
	tlsr8258_analog_write(base, value);
}

static int tlsr8258_uart_set_pin_func(uint32_t pin)
{
	if (pin == GPIO_PA0) {
		TLSR8258_REG_MUX_A1 = (TLSR8258_REG_MUX_A1 & (uint8_t)~GENMASK(1, 0)) | BIT(1);
	} else if (pin == GPIO_PA2) {
		TLSR8258_REG_MUX_A1 = (TLSR8258_REG_MUX_A1 & (uint8_t)~GENMASK(5, 4)) | BIT(4);
	} else {
		return -ENOTSUP;
	}

	TLSR8258_REG_GPIO_FUNC(pin) &= (uint8_t)~(pin & 0xffu);

	return 0;
}

static int tlsr8258_uart_configure_pin(uint32_t pin)
{
	if (pin == 0u) {
		return 0;
	}

	TLSR8258_REG_GPIO_IE(pin) |= (uint8_t)(pin & 0xffu);
	tlsr8258_uart_pullup(pin);

	return tlsr8258_uart_set_pin_func(pin);
}

static void tlsr8258_uart_hw_init(uint32_t clock, uint32_t baudrate)
{
	uint16_t divider;
	uint8_t bwpc;

	TLSR8258_REG_CLK_EN0 |= FLD_CLK0_UART_EN;
	TLSR8258_REG_RST0 |= FLD_RST0_UART;
	TLSR8258_REG_RST0 &= (uint8_t)~FLD_RST0_UART;

	tlsr8258_uart_cal_div_and_bwpc(baudrate, clock, &divider, &bwpc);

	TLSR8258_REG_UART_CTRL0 = (TLSR8258_REG_UART_CTRL0 & (uint8_t)~FLD_UART_BPWC) | bwpc;
	TLSR8258_REG_UART_CLK_DIV = divider | FLD_UART_CLK_DIV_EN;
	TLSR8258_REG_UART_RX_TIMEOUT0 = (bwpc + 1u) * 12u;
	TLSR8258_REG_UART_RX_TIMEOUT1 =
		(TLSR8258_REG_UART_RX_TIMEOUT1 & (uint8_t)~FLD_UART_TIMEOUT_MUL) | 1u;
	TLSR8258_REG_UART_CTRL1 &= (uint8_t)~(FLD_UART_PARITY_EN | FLD_UART_STOP_BIT);
}

static int tlsr8258_uart_init(const struct device *dev)
{
	const struct tlsr8258_uart_config *config = dev->config;
	struct tlsr8258_uart_data *data = dev->data;
	int ret;

	data->tx_index = 0u;
	data->rx_index = 0u;

#ifdef CONFIG_PINCTRL
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}
#else
	ret = tlsr8258_uart_configure_pin(config->tx_pin);
	if (ret < 0) {
		return ret;
	}

	ret = tlsr8258_uart_configure_pin(config->rx_pin);
	if (ret < 0) {
		return ret;
	}
#endif

	tlsr8258_uart_hw_init(config->clock_frequency, config->current_speed);

	return 0;
}

static void tlsr8258_uart_poll_out(const struct device *dev, unsigned char c)
{
	struct tlsr8258_uart_data *data = dev->data;

	while (tlsr8258_uart_tx_count() >= UART_TX_BUF_CNT_MAX) {
	}

	TLSR8258_REG_UART_DATA_BUF(data->tx_index) = c;
	data->tx_index = (data->tx_index + 1u) % UART_DATA_BUF_COUNT;
}

static int tlsr8258_uart_poll_in(const struct device *dev, unsigned char *c)
{
	struct tlsr8258_uart_data *data = dev->data;

	if (tlsr8258_uart_rx_count() == 0u) {
		return -1;
	}

	*c = TLSR8258_REG_UART_DATA_BUF(data->rx_index);
	data->rx_index = (data->rx_index + 1u) % UART_DATA_BUF_COUNT;

	return 0;
}

static int tlsr8258_uart_err_check(const struct device *dev)
{
	ARG_UNUSED(dev);

	return (TLSR8258_REG_UART_STATUS0 & FLD_UART_RX_ERR_FLAG) != 0u ? 1 : 0;
}

static DEVICE_API(uart, tlsr8258_uart_driver_api) = {
	.poll_in = tlsr8258_uart_poll_in,
	.poll_out = tlsr8258_uart_poll_out,
	.err_check = tlsr8258_uart_err_check,
};

#ifdef CONFIG_PINCTRL
#define TLSR8258_UART_PINCTRL_DEFINE(n) PINCTRL_DT_INST_DEFINE(n);
#define TLSR8258_UART_PINCTRL_CONFIG(n) \
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),
#else
#define TLSR8258_UART_PINCTRL_DEFINE(n)
#define TLSR8258_UART_PINCTRL_CONFIG(n)
#endif

#define TLSR8258_UART_INIT(n)							\
	TLSR8258_UART_PINCTRL_DEFINE(n)					\
	static const struct tlsr8258_uart_config tlsr8258_uart_config_##n = {	\
		.clock_frequency = DT_INST_PROP(n, clock_frequency),		\
		.current_speed = DT_INST_PROP(n, current_speed),		\
		.tx_pin = DT_INST_PROP(n, tx_pin),				\
		.rx_pin = DT_INST_PROP_OR(n, rx_pin, 0),			\
		TLSR8258_UART_PINCTRL_CONFIG(n)				\
	};									\
										\
	static struct tlsr8258_uart_data tlsr8258_uart_data_##n;		\
										\
	DEVICE_DT_INST_DEFINE(n, tlsr8258_uart_init, NULL,			\
			      &tlsr8258_uart_data_##n,			\
			      &tlsr8258_uart_config_##n, PRE_KERNEL_1,		\
			      CONFIG_SERIAL_INIT_PRIORITY,			\
			      &tlsr8258_uart_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_UART_INIT)
