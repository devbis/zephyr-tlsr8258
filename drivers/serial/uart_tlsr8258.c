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
#include <tlsr825x/irq.h>

#define TLSR8258_REG8(addr)  (*(volatile uint8_t *)(addr))
#define TLSR8258_REG16(addr) (*(volatile uint16_t *)(addr))

#define TLSR8258_REG_RST0    TLSR8258_REG8(0x00800060u)
#define TLSR8258_REG_CLK_EN0 TLSR8258_REG8(0x00800063u)

#define TLSR8258_REG_UART_DATA_BUF(n) TLSR8258_REG8(0x00800090u + (n))
#define TLSR8258_REG_UART_CLK_DIV     TLSR8258_REG16(0x00800094u)
#define TLSR8258_REG_UART_CTRL0       TLSR8258_REG8(0x00800096u)
#define TLSR8258_REG_UART_CTRL1       TLSR8258_REG8(0x00800097u)
#define TLSR8258_REG_UART_CTRL3       TLSR8258_REG8(0x00800099u)
#define TLSR8258_REG_UART_RX_TIMEOUT0 TLSR8258_REG8(0x0080009au)
#define TLSR8258_REG_UART_RX_TIMEOUT1 TLSR8258_REG8(0x0080009bu)
#define TLSR8258_REG_UART_BUF_CNT     TLSR8258_REG8(0x0080009cu)
#define TLSR8258_REG_UART_STATUS0     TLSR8258_REG8(0x0080009du)
#define TLSR8258_REG_UART_STATUS1     TLSR8258_REG8(0x0080009eu)

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
#define FLD_UART_RX_IRQ_EN   BIT(6)
#define FLD_UART_TX_IRQ_EN   BIT(7)
#define FLD_UART_TIMEOUT_MUL GENMASK(1, 0)
#define FLD_UART_MASK_TXDONE_IRQ BIT(6)
#define FLD_UART_MASK_ERR_IRQ BIT(7)
#define FLD_UART_PARITY_EN   BIT(2)
#define FLD_UART_STOP_BIT    GENMASK(5, 4)
#define FLD_UART_RX_IRQ_TRIG_LEV GENMASK(3, 0)
#define FLD_UART_TX_IRQ_TRIG_LEV GENMASK(7, 4)
#define FLD_UART_RX_BUF_CNT  GENMASK(3, 0)
#define FLD_UART_TX_BUF_CNT  GENMASK(7, 4)
#define FLD_UART_IRQ_FLAG    BIT(3)
#define FLD_UART_CLEAR_RX_FLAG BIT(6)
#define FLD_UART_RX_ERR_FLAG BIT(7)
#define FLD_UART_TX_DONE     BIT(0)
#define FLD_UART_TX_BUF_IRQ  BIT(1)
#define FLD_UART_RX_DONE     BIT(2)
#define FLD_UART_RX_BUF_IRQ  BIT(3)
#define FLD_UART_STATUS1_IRQ_MASK \
	(FLD_UART_TX_DONE | FLD_UART_TX_BUF_IRQ | FLD_UART_RX_DONE | FLD_UART_RX_BUF_IRQ)
#define FLD_ANA_BUSY         BIT(0)
#define FLD_ANA_RW           BIT(5)
#define FLD_ANA_CYC0         BIT(6)

#define GPIO_PA0 BIT(0)
#define GPIO_PA2 BIT(2)
#define UART_TX_BUF_CNT_MAX 8u
#define UART_DATA_BUF_COUNT 4u
#define UART_RX_IRQ_TRIGGER_LEVEL 1u
#define UART_TX_IRQ_TRIGGER_LEVEL 4u
#define PM_PIN_PULLUP_10K 3u

struct tlsr8258_uart_config {
	uint32_t clock_frequency;
	uint32_t current_speed;
	uint32_t tx_pin;
	uint32_t rx_pin;
#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	void (*irq_config_func)(const struct device *dev);
#endif
#ifdef CONFIG_PINCTRL
	const struct pinctrl_dev_config *pcfg;
#endif
};

struct tlsr8258_uart_data {
	uint8_t tx_index;
	uint8_t rx_index;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback;
	void *callback_data;
	uint8_t irq_status;
#endif
#ifdef CONFIG_UART_ASYNC_API
	uart_callback_t async_callback;
	void *async_user_data;
	const uint8_t *async_tx_buf;
	size_t async_tx_len;
	size_t async_tx_pos;
	uint8_t *async_rx_buf;
	size_t async_rx_len;
	size_t async_rx_pos;
	uint8_t *async_rx_next_buf;
	size_t async_rx_next_len;
#endif
};

#ifndef CONFIG_PINCTRL
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
#endif /* CONFIG_PINCTRL */

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

static void tlsr8258_uart_clear_irq_status(uint8_t mask)
{
	TLSR8258_REG_UART_STATUS1 = mask;
	*TLSR8258_REG_IRQ_SRC = BIT(TLSR8258_IRQ_UART);
}

#ifndef CONFIG_PINCTRL
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
#endif /* CONFIG_PINCTRL */

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
	TLSR8258_REG_UART_CTRL3 = (UART_RX_IRQ_TRIGGER_LEVEL & FLD_UART_RX_IRQ_TRIG_LEV) |
				  ((UART_TX_IRQ_TRIGGER_LEVEL << 4) & FLD_UART_TX_IRQ_TRIG_LEV);
	TLSR8258_REG_UART_RX_TIMEOUT0 = (bwpc + 1u) * 12u;
	TLSR8258_REG_UART_RX_TIMEOUT1 =
		(TLSR8258_REG_UART_RX_TIMEOUT1 & (uint8_t)~FLD_UART_TIMEOUT_MUL) |
		FLD_UART_MASK_TXDONE_IRQ | FLD_UART_MASK_ERR_IRQ | 1u;
	TLSR8258_REG_UART_CTRL1 &= (uint8_t)~(FLD_UART_PARITY_EN | FLD_UART_STOP_BIT);
	TLSR8258_REG_UART_CTRL0 &= (uint8_t)~(FLD_UART_RX_IRQ_EN | FLD_UART_TX_IRQ_EN);
	TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	tlsr8258_uart_clear_irq_status(FLD_UART_STATUS1_IRQ_MASK);
}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
static void tlsr8258_uart_parent_irq_update(void)
{
	if ((TLSR8258_REG_UART_CTRL0 & (FLD_UART_RX_IRQ_EN | FLD_UART_TX_IRQ_EN)) != 0u ||
	    (TLSR8258_REG_UART_RX_TIMEOUT1 & FLD_UART_MASK_ERR_IRQ) == 0u) {
		irq_enable(TLSR8258_IRQ_UART);
	} else {
		irq_disable(TLSR8258_IRQ_UART);
	}
}
#endif

static int tlsr8258_uart_init(const struct device *dev)
{
	const struct tlsr8258_uart_config *config = dev->config;
	struct tlsr8258_uart_data *data = dev->data;
	int ret;

	data->tx_index = 0u;
	data->rx_index = 0u;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	data->callback = NULL;
	data->callback_data = NULL;
	data->irq_status = 0u;
#endif
#ifdef CONFIG_UART_ASYNC_API
	data->async_callback = NULL;
	data->async_user_data = NULL;
	data->async_tx_buf = NULL;
	data->async_tx_len = 0u;
	data->async_tx_pos = 0u;
	data->async_rx_buf = NULL;
	data->async_rx_len = 0u;
	data->async_rx_pos = 0u;
	data->async_rx_next_buf = NULL;
	data->async_rx_next_len = 0u;
#endif

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

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
	config->irq_config_func(dev);
	irq_disable(TLSR8258_IRQ_UART);
#endif

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

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static void tlsr8258_uart_clear_disabled_irq_status(void)
{
	uint8_t clear = 0u;

	if ((TLSR8258_REG_UART_CTRL0 & FLD_UART_TX_IRQ_EN) == 0u) {
		clear |= FLD_UART_TX_DONE | FLD_UART_TX_BUF_IRQ;
	}

	if ((TLSR8258_REG_UART_CTRL0 & FLD_UART_RX_IRQ_EN) == 0u) {
		clear |= FLD_UART_RX_DONE | FLD_UART_RX_BUF_IRQ;
		TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	}

	if ((TLSR8258_REG_UART_RX_TIMEOUT1 & FLD_UART_MASK_ERR_IRQ) != 0u) {
		TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	}

	if ((TLSR8258_REG_UART_STATUS1 & clear) != 0u) {
		tlsr8258_uart_clear_irq_status(clear);
	}
}

static int tlsr8258_uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
	struct tlsr8258_uart_data *data = dev->data;
	int num_tx = 0;

	while (num_tx < len && tlsr8258_uart_tx_count() < UART_TX_BUF_CNT_MAX) {
		TLSR8258_REG_UART_DATA_BUF(data->tx_index) = tx_data[num_tx++];
		data->tx_index = (data->tx_index + 1u) % UART_DATA_BUF_COUNT;
	}

	return num_tx;
}

static int tlsr8258_uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
	struct tlsr8258_uart_data *data = dev->data;
	int num_rx = 0;

	while (num_rx < size && tlsr8258_uart_rx_count() != 0u) {
		rx_data[num_rx++] = TLSR8258_REG_UART_DATA_BUF(data->rx_index);
		data->rx_index = (data->rx_index + 1u) % UART_DATA_BUF_COUNT;
	}

	if (tlsr8258_uart_rx_count() == 0u) {
		TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	}

	return num_rx;
}

static void tlsr8258_uart_irq_tx_enable(const struct device *dev)
{
	ARG_UNUSED(dev);

	unsigned int key = irq_lock();

	tlsr8258_uart_clear_irq_status(FLD_UART_STATUS1_IRQ_MASK);
	TLSR8258_REG_UART_CTRL0 |= FLD_UART_TX_IRQ_EN;
	tlsr8258_uart_parent_irq_update();
	irq_unlock(key);
}

static void tlsr8258_uart_irq_tx_disable(const struct device *dev)
{
	ARG_UNUSED(dev);

	unsigned int key = irq_lock();

	TLSR8258_REG_UART_CTRL0 &= (uint8_t)~FLD_UART_TX_IRQ_EN;
	tlsr8258_uart_clear_irq_status(FLD_UART_TX_DONE | FLD_UART_TX_BUF_IRQ);
	tlsr8258_uart_parent_irq_update();
	irq_unlock(key);
}

static int tlsr8258_uart_irq_tx_ready(const struct device *dev)
{
	ARG_UNUSED(dev);

	return tlsr8258_uart_tx_count() < UART_TX_BUF_CNT_MAX ? 1 : 0;
}

static void tlsr8258_uart_irq_rx_enable(const struct device *dev)
{
	ARG_UNUSED(dev);

	unsigned int key = irq_lock();

	TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	if ((TLSR8258_REG_UART_CTRL0 & FLD_UART_TX_IRQ_EN) == 0u) {
		tlsr8258_uart_clear_irq_status(FLD_UART_TX_DONE | FLD_UART_TX_BUF_IRQ);
	}
	TLSR8258_REG_UART_CTRL0 |= FLD_UART_RX_IRQ_EN;
	tlsr8258_uart_parent_irq_update();
	irq_unlock(key);
}

static void tlsr8258_uart_irq_rx_disable(const struct device *dev)
{
	ARG_UNUSED(dev);

	unsigned int key = irq_lock();

	TLSR8258_REG_UART_CTRL0 &= (uint8_t)~FLD_UART_RX_IRQ_EN;
	TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	tlsr8258_uart_clear_irq_status(FLD_UART_RX_DONE | FLD_UART_RX_BUF_IRQ);
	tlsr8258_uart_parent_irq_update();
	irq_unlock(key);
}

static int tlsr8258_uart_irq_tx_complete(const struct device *dev)
{
	ARG_UNUSED(dev);

	return (tlsr8258_uart_tx_count() == 0u &&
		(TLSR8258_REG_UART_STATUS1 & FLD_UART_TX_DONE) != 0u) ? 1 : 0;
}

static int tlsr8258_uart_irq_rx_ready(const struct device *dev)
{
	ARG_UNUSED(dev);

	return ((TLSR8258_REG_UART_CTRL0 & FLD_UART_RX_IRQ_EN) != 0u &&
		tlsr8258_uart_rx_count() != 0u) ? 1 : 0;
}

static void tlsr8258_uart_irq_err_enable(const struct device *dev)
{
	ARG_UNUSED(dev);

	unsigned int key = irq_lock();

	TLSR8258_REG_UART_RX_TIMEOUT1 &= (uint8_t)~FLD_UART_MASK_ERR_IRQ;
	tlsr8258_uart_parent_irq_update();
	irq_unlock(key);
}

static void tlsr8258_uart_irq_err_disable(const struct device *dev)
{
	ARG_UNUSED(dev);

	unsigned int key = irq_lock();

	TLSR8258_REG_UART_RX_TIMEOUT1 |= FLD_UART_MASK_ERR_IRQ;
	TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	tlsr8258_uart_clear_irq_status(FLD_UART_RX_DONE | FLD_UART_RX_BUF_IRQ);
	tlsr8258_uart_parent_irq_update();
	irq_unlock(key);
}

static int tlsr8258_uart_irq_is_pending(const struct device *dev)
{
	ARG_UNUSED(dev);

	bool rx_pending = (TLSR8258_REG_UART_CTRL0 & FLD_UART_RX_IRQ_EN) != 0u &&
			  tlsr8258_uart_rx_count() != 0u;
	bool tx_pending = (TLSR8258_REG_UART_CTRL0 & FLD_UART_TX_IRQ_EN) != 0u &&
			  tlsr8258_uart_tx_count() <= UART_TX_IRQ_TRIGGER_LEVEL;
	bool err_pending = (TLSR8258_REG_UART_RX_TIMEOUT1 & FLD_UART_MASK_ERR_IRQ) == 0u &&
			   (TLSR8258_REG_UART_STATUS0 & FLD_UART_RX_ERR_FLAG) != 0u;

	return (rx_pending || tx_pending || err_pending) ? 1 : 0;
}

static int tlsr8258_uart_irq_update(const struct device *dev)
{
	struct tlsr8258_uart_data *data = dev->data;

	data->irq_status = TLSR8258_REG_UART_STATUS1;
	return 1;
}

static void tlsr8258_uart_irq_callback_set(const struct device *dev,
					   uart_irq_callback_user_data_t cb,
					   void *user_data)
{
	struct tlsr8258_uart_data *data = dev->data;

	data->callback = cb;
	data->callback_data = user_data;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#ifdef CONFIG_UART_ASYNC_API
static void tlsr8258_uart_async_isr(const struct device *dev);
#endif

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
static void tlsr8258_uart_isr(const void *arg)
{
	const struct device *dev = arg;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	struct tlsr8258_uart_data *data = dev->data;

	tlsr8258_uart_clear_disabled_irq_status();
	data->irq_status = TLSR8258_REG_UART_STATUS1;

	if (data->callback != NULL && tlsr8258_uart_irq_is_pending(dev)) {
		data->callback(dev, data->callback_data);
		return;
	}
#endif

#ifdef CONFIG_UART_ASYNC_API
	tlsr8258_uart_async_isr(dev);
	if (((TLSR8258_REG_UART_CTRL0 & (FLD_UART_RX_IRQ_EN | FLD_UART_TX_IRQ_EN)) != 0u)) {
		return;
	}
#endif

	TLSR8258_REG_UART_CTRL0 &= (uint8_t)~(FLD_UART_RX_IRQ_EN | FLD_UART_TX_IRQ_EN);
	TLSR8258_REG_UART_RX_TIMEOUT1 |= FLD_UART_MASK_ERR_IRQ;
	tlsr8258_uart_parent_irq_update();
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN || CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_ASYNC_API
static void tlsr8258_uart_async_callback(const struct device *dev, struct uart_event *evt)
{
	struct tlsr8258_uart_data *data = dev->data;

	if (data->async_callback != NULL) {
		data->async_callback(dev, evt, data->async_user_data);
	}
}

static void tlsr8258_uart_async_tx_fill(struct tlsr8258_uart_data *data)
{
	while (data->async_tx_pos < data->async_tx_len &&
	       tlsr8258_uart_tx_count() < UART_TX_BUF_CNT_MAX) {
		TLSR8258_REG_UART_DATA_BUF(data->tx_index) =
			data->async_tx_buf[data->async_tx_pos++];
		data->tx_index = (data->tx_index + 1u) % UART_DATA_BUF_COUNT;
	}
}

static void tlsr8258_uart_async_tx_done(const struct device *dev, bool aborted)
{
	struct tlsr8258_uart_data *data = dev->data;
	struct uart_event evt = {
		.type = aborted ? UART_TX_ABORTED : UART_TX_DONE,
		.data.tx = {
			.buf = data->async_tx_buf,
			.len = data->async_tx_pos,
		},
	};

	TLSR8258_REG_UART_CTRL0 &= (uint8_t)~FLD_UART_TX_IRQ_EN;
	data->async_tx_buf = NULL;
	data->async_tx_len = 0u;
	data->async_tx_pos = 0u;
	tlsr8258_uart_parent_irq_update();
	tlsr8258_uart_async_callback(dev, &evt);
}

static void tlsr8258_uart_async_rx_release(const struct device *dev, bool disabled)
{
	struct tlsr8258_uart_data *data = dev->data;
	uint8_t *buf = data->async_rx_buf;
	struct uart_event evt = {
		.type = UART_RX_BUF_RELEASED,
		.data.rx_buf = {
			.buf = buf,
		},
	};

	tlsr8258_uart_async_callback(dev, &evt);

	if (data->async_rx_next_buf != NULL && !disabled) {
		struct uart_event req_evt = {
			.type = UART_RX_BUF_REQUEST,
		};

		data->async_rx_buf = data->async_rx_next_buf;
		data->async_rx_len = data->async_rx_next_len;
		data->async_rx_pos = 0u;
		data->async_rx_next_buf = NULL;
		data->async_rx_next_len = 0u;
		tlsr8258_uart_async_callback(dev, &req_evt);
		return;
	}

	data->async_rx_buf = NULL;
	data->async_rx_len = 0u;
	data->async_rx_pos = 0u;
	TLSR8258_REG_UART_CTRL0 &= (uint8_t)~FLD_UART_RX_IRQ_EN;
	tlsr8258_uart_parent_irq_update();

	evt.type = UART_RX_DISABLED;
	tlsr8258_uart_async_callback(dev, &evt);
}

static void tlsr8258_uart_async_rx_drain(const struct device *dev)
{
	struct tlsr8258_uart_data *data = dev->data;
	size_t start;

	if (data->async_rx_buf == NULL) {
		return;
	}

	start = data->async_rx_pos;
	while (data->async_rx_pos < data->async_rx_len && tlsr8258_uart_rx_count() != 0u) {
		data->async_rx_buf[data->async_rx_pos++] =
			TLSR8258_REG_UART_DATA_BUF(data->rx_index);
		data->rx_index = (data->rx_index + 1u) % UART_DATA_BUF_COUNT;
	}

	if (tlsr8258_uart_rx_count() == 0u) {
		TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	}

	if (data->async_rx_pos != start) {
		struct uart_event evt = {
			.type = UART_RX_RDY,
			.data.rx = {
				.buf = data->async_rx_buf,
				.offset = start,
				.len = data->async_rx_pos - start,
			},
		};

		tlsr8258_uart_async_callback(dev, &evt);
	}

	if (data->async_rx_pos == data->async_rx_len) {
		tlsr8258_uart_async_rx_release(dev, false);
	}
}

static int tlsr8258_uart_callback_set(const struct device *dev,
				      uart_callback_t callback, void *user_data)
{
	struct tlsr8258_uart_data *data = dev->data;

	data->async_callback = callback;
	data->async_user_data = user_data;
	return 0;
}

static int tlsr8258_uart_tx(const struct device *dev, const uint8_t *buf,
			    size_t len, int32_t timeout)
{
	struct tlsr8258_uart_data *data = dev->data;
	unsigned int key;

	ARG_UNUSED(timeout);

	if (buf == NULL || len == 0u) {
		return -EINVAL;
	}

	key = irq_lock();
	if (data->async_tx_buf != NULL) {
		irq_unlock(key);
		return -EBUSY;
	}

	data->async_tx_buf = buf;
	data->async_tx_len = len;
	data->async_tx_pos = 0u;
	tlsr8258_uart_async_tx_fill(data);
	TLSR8258_REG_UART_CTRL0 |= FLD_UART_TX_IRQ_EN;
	tlsr8258_uart_parent_irq_update();
	irq_unlock(key);

	return 0;
}

static int tlsr8258_uart_tx_abort(const struct device *dev)
{
	struct tlsr8258_uart_data *data = dev->data;
	unsigned int key = irq_lock();

	if (data->async_tx_buf == NULL) {
		irq_unlock(key);
		return -EFAULT;
	}

	tlsr8258_uart_async_tx_done(dev, true);
	irq_unlock(key);
	return 0;
}

static int tlsr8258_uart_rx_enable(const struct device *dev, uint8_t *buf,
				   size_t len, int32_t timeout)
{
	struct tlsr8258_uart_data *data = dev->data;
	struct uart_event evt = {
		.type = UART_RX_BUF_REQUEST,
	};
	unsigned int key;

	ARG_UNUSED(timeout);

	if (buf == NULL || len == 0u) {
		return -EINVAL;
	}

	key = irq_lock();
	if (data->async_rx_buf != NULL) {
		irq_unlock(key);
		return -EBUSY;
	}

	data->async_rx_buf = buf;
	data->async_rx_len = len;
	data->async_rx_pos = 0u;
	data->async_rx_next_buf = NULL;
	data->async_rx_next_len = 0u;
	TLSR8258_REG_UART_STATUS0 = FLD_UART_CLEAR_RX_FLAG;
	TLSR8258_REG_UART_CTRL0 |= FLD_UART_RX_IRQ_EN;
	tlsr8258_uart_parent_irq_update();
	tlsr8258_uart_async_callback(dev, &evt);
	irq_unlock(key);

	return 0;
}

static int tlsr8258_uart_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	struct tlsr8258_uart_data *data = dev->data;
	unsigned int key;

	if (buf == NULL || len == 0u) {
		return -EINVAL;
	}

	key = irq_lock();
	if (data->async_rx_buf == NULL) {
		irq_unlock(key);
		return -EACCES;
	}
	if (data->async_rx_next_buf != NULL) {
		irq_unlock(key);
		return -EBUSY;
	}

	data->async_rx_next_buf = buf;
	data->async_rx_next_len = len;
	irq_unlock(key);
	return 0;
}

static int tlsr8258_uart_rx_disable(const struct device *dev)
{
	struct tlsr8258_uart_data *data = dev->data;
	unsigned int key = irq_lock();

	if (data->async_rx_buf == NULL) {
		irq_unlock(key);
		return -EFAULT;
	}

	tlsr8258_uart_async_rx_drain(dev);
	tlsr8258_uart_async_rx_release(dev, true);
	irq_unlock(key);
	return 0;
}

static void tlsr8258_uart_async_isr(const struct device *dev)
{
	struct tlsr8258_uart_data *data = dev->data;

	if (data->async_rx_buf != NULL && tlsr8258_uart_rx_count() != 0u) {
		tlsr8258_uart_async_rx_drain(dev);
	}

	if (data->async_tx_buf != NULL) {
		tlsr8258_uart_async_tx_fill(data);
		if (data->async_tx_pos == data->async_tx_len &&
		    tlsr8258_uart_tx_count() == 0u &&
		    (TLSR8258_REG_UART_STATUS1 & FLD_UART_TX_DONE) != 0u) {
			tlsr8258_uart_async_tx_done(dev, false);
		}
	}
}
#endif /* CONFIG_UART_ASYNC_API */

static DEVICE_API(uart, tlsr8258_uart_driver_api) = {
	.poll_in = tlsr8258_uart_poll_in,
	.poll_out = tlsr8258_uart_poll_out,
	.err_check = tlsr8258_uart_err_check,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = tlsr8258_uart_fifo_fill,
	.fifo_read = tlsr8258_uart_fifo_read,
	.irq_tx_enable = tlsr8258_uart_irq_tx_enable,
	.irq_tx_disable = tlsr8258_uart_irq_tx_disable,
	.irq_tx_ready = tlsr8258_uart_irq_tx_ready,
	.irq_rx_enable = tlsr8258_uart_irq_rx_enable,
	.irq_rx_disable = tlsr8258_uart_irq_rx_disable,
	.irq_tx_complete = tlsr8258_uart_irq_tx_complete,
	.irq_rx_ready = tlsr8258_uart_irq_rx_ready,
	.irq_err_enable = tlsr8258_uart_irq_err_enable,
	.irq_err_disable = tlsr8258_uart_irq_err_disable,
	.irq_is_pending = tlsr8258_uart_irq_is_pending,
	.irq_update = tlsr8258_uart_irq_update,
	.irq_callback_set = tlsr8258_uart_irq_callback_set,
#endif
#ifdef CONFIG_UART_ASYNC_API
	.callback_set = tlsr8258_uart_callback_set,
	.tx = tlsr8258_uart_tx,
	.tx_abort = tlsr8258_uart_tx_abort,
	.rx_enable = tlsr8258_uart_rx_enable,
	.rx_buf_rsp = tlsr8258_uart_rx_buf_rsp,
	.rx_disable = tlsr8258_uart_rx_disable,
#endif
};

#ifdef CONFIG_PINCTRL
#define TLSR8258_UART_PINCTRL_DEFINE(n) PINCTRL_DT_INST_DEFINE(n);
#define TLSR8258_UART_PINCTRL_CONFIG(n) \
	.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),
#else
#define TLSR8258_UART_PINCTRL_DEFINE(n)
#define TLSR8258_UART_PINCTRL_CONFIG(n)
#endif

#if defined(CONFIG_UART_INTERRUPT_DRIVEN) || defined(CONFIG_UART_ASYNC_API)
#define TLSR8258_UART_IRQ_CONFIG_DEFINE(n)					\
	static void tlsr8258_uart_irq_config_##n(const struct device *dev)	\
	{									\
		ARG_UNUSED(dev);						\
		IRQ_CONNECT(DT_INST_IRQN(n), 0, tlsr8258_uart_isr,		\
			    DEVICE_DT_INST_GET(n), 0);				\
	}
#define TLSR8258_UART_IRQ_CONFIG(n) \
	.irq_config_func = tlsr8258_uart_irq_config_##n,
#else
#define TLSR8258_UART_IRQ_CONFIG_DEFINE(n)
#define TLSR8258_UART_IRQ_CONFIG(n)
#endif

#define TLSR8258_UART_INIT(n)							\
	TLSR8258_UART_PINCTRL_DEFINE(n)					\
	TLSR8258_UART_IRQ_CONFIG_DEFINE(n)				\
	static const struct tlsr8258_uart_config tlsr8258_uart_config_##n = {	\
		.clock_frequency = DT_INST_PROP(n, clock_frequency),		\
		.current_speed = DT_INST_PROP(n, current_speed),		\
		.tx_pin = DT_INST_PROP(n, tx_pin),				\
		.rx_pin = DT_INST_PROP_OR(n, rx_pin, 0),			\
		TLSR8258_UART_IRQ_CONFIG(n)				\
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
