/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT wch_ch57x_uart

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* Register offsets */
#define UART_MCR 0x00U
#define UART_IER 0x01U
#define UART_FCR 0x02U
#define UART_LCR 0x03U
#define UART_IIR 0x04U
#define UART_LSR 0x05U
#define UART_RBR 0x08U /* read: receive buffer, write: transmit holding */
#define UART_RFC 0x0aU
#define UART_TFC 0x0bU
#define UART_DL  0x0cU /* 16 bit */
#define UART_DIV 0x0eU

#define MCR_INT_OE 0x08U

#define IER_RECV_RDY  0x01U
#define IER_THR_EMPTY 0x02U
#define IER_LINE_STAT 0x04U
#define IER_TXD_EN    0x40U

#define FCR_FIFO_EN     0x01U
#define FCR_RX_FIFO_CLR 0x02U
#define FCR_TX_FIFO_CLR 0x04U
#define FCR_TRIG_4B     0x80U

#define LCR_STOP_BIT 0x04U
#define LCR_PAR_EN   0x08U
#define LCR_PAR_MOD  0x30U

#define IIR_NO_INT   0x01U
#define IIR_INT_MASK 0x0fU

#define LSR_DATA_RDY    0x01U
#define LSR_OVER_ERR    0x02U
#define LSR_PAR_ERR     0x04U
#define LSR_FRAME_ERR   0x08U
#define LSR_BREAK_ERR   0x10U
#define LSR_TX_FIFO_EMP 0x20U
#define LSR_TX_ALL_EMP  0x40U

#define UART_FIFO_SIZE 8U

struct uart_ch57x_config {
	uintptr_t base;
	const struct pinctrl_dev_config *pcfg;
	uint32_t clock_frequency;
	uint32_t baud;
	uint8_t parity;
	uint8_t stop_bits;
	uint8_t data_bits;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	void (*irq_config)(const struct device *dev);
#endif
};

struct uart_ch57x_data {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t cb;
	void *cb_data;
#endif
};

static int uart_ch57x_init(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;
	uint8_t lcr = 0U;
	uint32_t dl;
	int err;

	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_5:
	case UART_CFG_DATA_BITS_6:
	case UART_CFG_DATA_BITS_7:
	case UART_CFG_DATA_BITS_8:
		lcr |= cfg->data_bits; /* enum values 0..3 map to the 5..8 bit encoding */
		break;
	default:
		return -ENOTSUP;
	}

	if (cfg->stop_bits == UART_CFG_STOP_BITS_2) {
		lcr |= LCR_STOP_BIT;
	} else if (cfg->stop_bits != UART_CFG_STOP_BITS_1) {
		return -ENOTSUP;
	}

	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		break;
	case UART_CFG_PARITY_ODD:
		lcr |= LCR_PAR_EN | (0U << 4);
		break;
	case UART_CFG_PARITY_EVEN:
		lcr |= LCR_PAR_EN | (1U << 4);
		break;
	case UART_CFG_PARITY_MARK:
		lcr |= LCR_PAR_EN | (2U << 4);
		break;
	case UART_CFG_PARITY_SPACE:
		lcr |= LCR_PAR_EN | (3U << 4);
		break;
	default:
		return -ENOTSUP;
	}

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		return err;
	}

	/* Baud rate = Fsys / (8 * DIV * DL) with DIV = 1, rounded to nearest */
	dl = DIV_ROUND_CLOSEST(cfg->clock_frequency, 8U * cfg->baud);

	sys_write8(IER_TXD_EN, cfg->base + UART_IER);
	sys_write8(lcr, cfg->base + UART_LCR);
	sys_write16((uint16_t)dl, cfg->base + UART_DL);
	sys_write8(1U, cfg->base + UART_DIV);
	sys_write8(FCR_FIFO_EN | FCR_RX_FIFO_CLR | FCR_TX_FIFO_CLR | FCR_TRIG_4B,
		   cfg->base + UART_FCR);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	sys_write8(MCR_INT_OE, cfg->base + UART_MCR);
	cfg->irq_config(dev);
#endif

	return 0;
}

static int uart_ch57x_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_ch57x_config *cfg = dev->config;

	if ((sys_read8(cfg->base + UART_LSR) & LSR_DATA_RDY) == 0U) {
		return -1;
	}

	*c = sys_read8(cfg->base + UART_RBR);

	return 0;
}

static void uart_ch57x_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_ch57x_config *cfg = dev->config;

	while (sys_read8(cfg->base + UART_TFC) >= UART_FIFO_SIZE) {
	}

	sys_write8(c, cfg->base + UART_RBR);
}

static int uart_ch57x_err_check(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;
	uint8_t lsr = sys_read8(cfg->base + UART_LSR);
	int errors = 0;

	if ((lsr & LSR_OVER_ERR) != 0U) {
		errors |= UART_ERROR_OVERRUN;
	}
	if ((lsr & LSR_PAR_ERR) != 0U) {
		errors |= UART_ERROR_PARITY;
	}
	if ((lsr & LSR_FRAME_ERR) != 0U) {
		errors |= UART_ERROR_FRAMING;
	}
	if ((lsr & LSR_BREAK_ERR) != 0U) {
		errors |= UART_BREAK;
	}

	return errors;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static void uart_ch57x_set_bits(uintptr_t addr, uint8_t bits, bool set)
{
	uint8_t val = sys_read8(addr);

	sys_write8(set ? (val | bits) : (val & ~bits), addr);
}

static int uart_ch57x_fifo_fill(const struct device *dev, const uint8_t *buf, int len)
{
	const struct uart_ch57x_config *cfg = dev->config;
	int n = 0;

	while (n < len && sys_read8(cfg->base + UART_TFC) < UART_FIFO_SIZE) {
		sys_write8(buf[n++], cfg->base + UART_RBR);
	}

	return n;
}

static int uart_ch57x_fifo_read(const struct device *dev, uint8_t *buf, const int len)
{
	const struct uart_ch57x_config *cfg = dev->config;
	int n = 0;

	while (n < len && sys_read8(cfg->base + UART_RFC) != 0U) {
		buf[n++] = sys_read8(cfg->base + UART_RBR);
	}

	return n;
}

static void uart_ch57x_irq_tx_enable(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	uart_ch57x_set_bits(cfg->base + UART_IER, IER_THR_EMPTY, true);
}

static void uart_ch57x_irq_tx_disable(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	uart_ch57x_set_bits(cfg->base + UART_IER, IER_THR_EMPTY, false);
}

static int uart_ch57x_irq_tx_ready(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	return ((sys_read8(cfg->base + UART_IER) & IER_THR_EMPTY) != 0U) &&
	       (sys_read8(cfg->base + UART_TFC) < UART_FIFO_SIZE);
}

static int uart_ch57x_irq_tx_complete(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	return (sys_read8(cfg->base + UART_LSR) & LSR_TX_ALL_EMP) != 0U;
}

static void uart_ch57x_irq_rx_enable(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	uart_ch57x_set_bits(cfg->base + UART_IER, IER_RECV_RDY, true);
}

static void uart_ch57x_irq_rx_disable(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	uart_ch57x_set_bits(cfg->base + UART_IER, IER_RECV_RDY, false);
}

static int uart_ch57x_irq_rx_ready(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	return (sys_read8(cfg->base + UART_LSR) & LSR_DATA_RDY) != 0U;
}

static void uart_ch57x_irq_err_enable(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	uart_ch57x_set_bits(cfg->base + UART_IER, IER_LINE_STAT, true);
}

static void uart_ch57x_irq_err_disable(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	uart_ch57x_set_bits(cfg->base + UART_IER, IER_LINE_STAT, false);
}

static int uart_ch57x_irq_is_pending(const struct device *dev)
{
	const struct uart_ch57x_config *cfg = dev->config;

	return (sys_read8(cfg->base + UART_IIR) & IIR_NO_INT) == 0U;
}

static void uart_ch57x_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void uart_ch57x_irq_callback_set(const struct device *dev,
					uart_irq_callback_user_data_t cb, void *user_data)
{
	struct uart_ch57x_data *data = dev->data;

	data->cb = cb;
	data->cb_data = user_data;
}

static void uart_ch57x_isr(const struct device *dev)
{
	struct uart_ch57x_data *data = dev->data;

	if (data->cb != NULL) {
		data->cb(dev, data->cb_data);
	}
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static DEVICE_API(uart, uart_ch57x_api) = {
	.poll_in = uart_ch57x_poll_in,
	.poll_out = uart_ch57x_poll_out,
	.err_check = uart_ch57x_err_check,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_ch57x_fifo_fill,
	.fifo_read = uart_ch57x_fifo_read,
	.irq_tx_enable = uart_ch57x_irq_tx_enable,
	.irq_tx_disable = uart_ch57x_irq_tx_disable,
	.irq_tx_ready = uart_ch57x_irq_tx_ready,
	.irq_tx_complete = uart_ch57x_irq_tx_complete,
	.irq_rx_enable = uart_ch57x_irq_rx_enable,
	.irq_rx_disable = uart_ch57x_irq_rx_disable,
	.irq_rx_ready = uart_ch57x_irq_rx_ready,
	.irq_err_enable = uart_ch57x_irq_err_enable,
	.irq_err_disable = uart_ch57x_irq_err_disable,
	.irq_is_pending = uart_ch57x_irq_is_pending,
	.irq_update = uart_ch57x_irq_update,
	.irq_callback_set = uart_ch57x_irq_callback_set,
#endif
};

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define UART_CH57X_IRQ_DECL(n) static void uart_ch57x_irq_config_##n(const struct device *dev);
#define UART_CH57X_IRQ_FIELD(n) .irq_config = uart_ch57x_irq_config_##n,
#define UART_CH57X_IRQ_DEFINE(n)                                                                   \
	static void uart_ch57x_irq_config_##n(const struct device *dev)                            \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), uart_ch57x_isr,             \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}
#else
#define UART_CH57X_IRQ_DECL(n)
#define UART_CH57X_IRQ_FIELD(n)
#define UART_CH57X_IRQ_DEFINE(n)
#endif

#define UART_CH57X_INIT(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	UART_CH57X_IRQ_DECL(n)                                                                     \
	static struct uart_ch57x_data uart_ch57x_data_##n;                                         \
	static const struct uart_ch57x_config uart_ch57x_cfg_##n = {                               \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.clock_frequency = DT_PROP(DT_NODELABEL(cpu0), clock_frequency),                 \
		.baud = DT_INST_PROP(n, current_speed),                                            \
		.parity = DT_INST_ENUM_IDX(n, parity),                                             \
		.stop_bits = DT_INST_ENUM_IDX(n, stop_bits),                                       \
		.data_bits = DT_INST_ENUM_IDX(n, data_bits),                                       \
		UART_CH57X_IRQ_FIELD(n)};                                                          \
	DEVICE_DT_INST_DEFINE(n, uart_ch57x_init, NULL, &uart_ch57x_data_##n,                      \
			      &uart_ch57x_cfg_##n, PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY,      \
			      &uart_ch57x_api);                                                    \
	UART_CH57X_IRQ_DEFINE(n)

DT_INST_FOREACH_STATUS_OKAY(UART_CH57X_INIT)
