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
 * Interrupt-driven API: the DMA interrupt line reports RX packet ready and TX
 * done through the DMA ready bits. The async API is not implemented yet.
 */

#define DT_DRV_COMPAT telink_tlsr8269_uart

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <tlsr82xx/irq.h>

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
#define TLSR8269_UART_TX_MAX      128u
#define TLSR8269_FLD_UART_IRQ_RX  BIT(0)
#define TLSR8269_FLD_UART_IRQ_TX  BIT(1)
#define TLSR8269_FLD_DMA_TX_DONE  BIT(1)
#define TLSR8269_REG_DMA_IRQ_MSK  TLSR8269_REG8(0x00800521u)
#define TLSR8269_UART_MIN_BWPC    3u
#define TLSR8269_UART_RX_MIN_LEN  16u
#define TLSR8269_UART_RX_MAX_LEN  4080u
#define TLSR8269_DMA_WINDOW_HI    0x80u
#define TLSR8269_FLD_UART_RX_ERR_MSK BIT(7)
#define TLSR8269_FLD_UART_CLR_ERR    BIT(6)

struct tlsr8269_uart_config {
	uint32_t clock_frequency;
	uint32_t current_speed;
	void (*irq_config_func)(const struct device *dev);
	const struct pinctrl_dev_config *pcfg;
};

struct tlsr8269_uart_data {
	uart_irq_callback_user_data_t callback;
	void *callback_data;
	uint8_t tx_pkt[TLSR8269_UART_PKT_HDR + TLSR8269_UART_TX_MAX] __aligned(4);
	uint8_t rx_buf[TLSR8269_UART_RX_BUF_SIZE] __aligned(16);
	uint16_t rx_pos;

	uart_callback_t async_callback;
	void *async_user_data;
	const uint8_t *async_tx_buf;
	size_t async_tx_len;
	size_t async_tx_pos;
	uint8_t *async_rx_buf;
	size_t async_rx_len;
	uint8_t *async_rx_next_buf;
	size_t async_rx_next_len;
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


static bool tlsr8269_dma_addr_ok(uintptr_t addr)
{
	return (addr >> 16) == TLSR8269_DMA_WINDOW_HI;
}

static int tlsr8269_uart_hw_init(const struct tlsr8269_uart_config *config,
				 struct tlsr8269_uart_data *data)
{
	uint16_t divider = 0u;
	uint8_t bwpc = 0u;
	uintptr_t rx_addr = (uintptr_t)data->rx_buf;

	/* The DMA address registers are 16 bits wide; the buffer must lie in the 0x80xxxx window. */
	if (!tlsr8269_dma_addr_ok(rx_addr)) {
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
	TLSR8269_REG_DMA_IRQ_MSK = 0u;

	/* Arm RX DMA: address, then buffer size in 16-byte units. */
	TLSR8269_REG_DMA0_ADDR = (uint16_t)rx_addr;
	TLSR8269_REG_DMA0_SIZE = (uint8_t)(TLSR8269_UART_RX_BUF_SIZE >> 4);

	return 0;
}

static bool tlsr8269_uart_tx_idle(void)
{
	return (TLSR8269_REG_UART_STATUS1 & TLSR8269_FLD_UART_STATUS1_TX_DONE) != 0u;
}

static void tlsr8269_uart_tx_start(struct tlsr8269_uart_data *data, uint32_t len)
{
	data->tx_pkt[0] = (uint8_t)len;
	data->tx_pkt[1] = (uint8_t)(len >> 8);
	data->tx_pkt[2] = (uint8_t)(len >> 16);
	data->tx_pkt[3] = (uint8_t)(len >> 24);

	TLSR8269_REG_DMA1_ADDR = (uint16_t)(uintptr_t)data->tx_pkt;
	TLSR8269_REG_DMA_TX_RDY0 = TLSR8269_FLD_DMA_UART_TX;
}

static void tlsr8269_uart_poll_out(const struct device *dev, unsigned char c)
{
	struct tlsr8269_uart_data *data = dev->data;

	while (!tlsr8269_uart_tx_idle()) {
	}

	data->tx_pkt[TLSR8269_UART_PKT_HDR] = c;
	tlsr8269_uart_tx_start(data, 1u);
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

	TLSR8269_REG_UART_STATUS0 |= TLSR8269_FLD_UART_CLR_ERR;

	return UART_ERROR_FRAMING;
}

static int tlsr8269_uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
	struct tlsr8269_uart_data *data = dev->data;
	uint32_t n;

	if (len <= 0 || !tlsr8269_uart_tx_idle()) {
		return 0;
	}

	n = MIN((uint32_t)len, TLSR8269_UART_TX_MAX);
	memcpy(&data->tx_pkt[TLSR8269_UART_PKT_HDR], tx_data, n);
	tlsr8269_uart_tx_start(data, n);

	return (int)n;
}

static int tlsr8269_uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
	int num = 0;
	unsigned char c;

	while (num < size && tlsr8269_uart_poll_in(dev, &c) == 0) {
		rx_data[num++] = c;
	}

	return num;
}

static void tlsr8269_uart_irq_enable_dma(uint8_t flag, bool enable)
{
	unsigned int key = irq_lock();

	if (enable) {
		TLSR8269_REG_DMA_IRQ_MSK |= flag;
		irq_enable(TLSR82XX_IRQ_DMA);
	} else {
		TLSR8269_REG_DMA_IRQ_MSK &= (uint8_t)~flag;
		if ((TLSR8269_REG_DMA_IRQ_MSK & (TLSR8269_FLD_UART_IRQ_RX | TLSR8269_FLD_UART_IRQ_TX)) ==
		    0u) {
			irq_disable(TLSR82XX_IRQ_DMA);
		}
	}

	irq_unlock(key);
}

static void tlsr8269_uart_irq_tx_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_TX, true);
}

static void tlsr8269_uart_irq_tx_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_TX, false);
}

static int tlsr8269_uart_irq_tx_ready(const struct device *dev)
{
	ARG_UNUSED(dev);
	return tlsr8269_uart_tx_idle() ? 1 : 0;
}

static int tlsr8269_uart_irq_tx_complete(const struct device *dev)
{
	ARG_UNUSED(dev);
	return tlsr8269_uart_tx_idle() ? 1 : 0;
}

static void tlsr8269_uart_irq_rx_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_RX, true);
}

static void tlsr8269_uart_irq_rx_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_RX, false);
}

static int tlsr8269_uart_irq_rx_ready(const struct device *dev)
{
	ARG_UNUSED(dev);
	return (TLSR8269_REG_DMA_RX_RDY0 & TLSR8269_FLD_DMA_RX_UART) != 0u ? 1 : 0;
}

static void tlsr8269_uart_irq_err_enable(const struct device *dev)
{
	unsigned int key = irq_lock();

	ARG_UNUSED(dev);
	TLSR8269_REG_UART_RX_TO1 &= (uint8_t)~TLSR8269_FLD_UART_RX_ERR_MSK;
	irq_enable(TLSR82XX_IRQ_UART);
	irq_unlock(key);
}

static void tlsr8269_uart_irq_err_disable(const struct device *dev)
{
	unsigned int key = irq_lock();

	ARG_UNUSED(dev);
	TLSR8269_REG_UART_RX_TO1 |= TLSR8269_FLD_UART_RX_ERR_MSK;
	irq_disable(TLSR82XX_IRQ_UART);
	irq_unlock(key);
}

static int tlsr8269_uart_irq_is_pending(const struct device *dev)
{
	ARG_UNUSED(dev);

	bool rx_pending = (TLSR8269_REG_DMA_IRQ_MSK & TLSR8269_FLD_UART_IRQ_RX) != 0u &&
			  (TLSR8269_REG_DMA_RX_RDY0 & TLSR8269_FLD_DMA_RX_UART) != 0u;
	bool tx_pending = (TLSR8269_REG_DMA_IRQ_MSK & TLSR8269_FLD_UART_IRQ_TX) != 0u &&
			  tlsr8269_uart_tx_idle();
	bool err_pending = (TLSR8269_REG_UART_RX_TO1 & TLSR8269_FLD_UART_RX_ERR_MSK) == 0u &&
			   (TLSR8269_REG_UART_STATUS0 & TLSR8269_FLD_UART_STATUS0_RX_ERR) != 0u;

	return (rx_pending || tx_pending || err_pending) ? 1 : 0;
}

static void tlsr8269_uart_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void tlsr8269_uart_irq_callback_set(const struct device *dev,
					   uart_irq_callback_user_data_t cb, void *user_data)
{
	struct tlsr8269_uart_data *data = dev->data;

	data->callback = cb;
	data->callback_data = user_data;
}

static void tlsr8269_uart_event(const struct device *dev, struct uart_event *evt)
{
	struct tlsr8269_uart_data *data = dev->data;

	if (data->async_callback != NULL) {
		data->async_callback(dev, evt, data->async_user_data);
	}
}

static void tlsr8269_uart_rx_arm(uint8_t *buf, size_t len)
{
	TLSR8269_REG_DMA0_ADDR = (uint16_t)(uintptr_t)buf;
	TLSR8269_REG_DMA0_SIZE = (uint8_t)(len >> 4);
}

/* Send the next chunk of the async TX buffer, or report completion. */
static void tlsr8269_uart_async_tx_isr(const struct device *dev)
{
	struct tlsr8269_uart_data *data = dev->data;
	struct uart_event evt;
	size_t n;

	if (data->async_tx_buf == NULL) {
		return;
	}

	if (data->async_tx_pos < data->async_tx_len) {
		n = MIN(data->async_tx_len - data->async_tx_pos, (size_t)TLSR8269_UART_TX_MAX);
		memcpy(&data->tx_pkt[TLSR8269_UART_PKT_HDR],
		       &data->async_tx_buf[data->async_tx_pos], n);
		data->async_tx_pos += n;
		tlsr8269_uart_tx_start(data, (uint32_t)n);
		return;
	}

	evt.type = UART_TX_DONE;
	evt.data.tx.buf = data->async_tx_buf;
	evt.data.tx.len = data->async_tx_len;

	data->async_tx_buf = NULL;
	data->async_tx_len = 0u;
	data->async_tx_pos = 0u;
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_TX, false);
	tlsr8269_uart_event(dev, &evt);
}

/* One RX packet landed in the async buffer: report it and re-arm DMA. */
static void tlsr8269_uart_async_rx_isr(const struct device *dev)
{
	struct tlsr8269_uart_data *data = dev->data;
	struct uart_event evt;
	uint32_t len;

	len = (uint32_t)data->async_rx_buf[0] | ((uint32_t)data->async_rx_buf[1] << 8) |
	      ((uint32_t)data->async_rx_buf[2] << 16) | ((uint32_t)data->async_rx_buf[3] << 24);
	if (len > data->async_rx_len - TLSR8269_UART_PKT_HDR) {
		len = data->async_rx_len - TLSR8269_UART_PKT_HDR;
	}

	TLSR8269_REG_DMA_RX_RDY0 = TLSR8269_FLD_DMA_RX_UART;

	if (len > 0u) {
		evt.type = UART_RX_RDY;
		evt.data.rx.buf = data->async_rx_buf;
		evt.data.rx.offset = TLSR8269_UART_PKT_HDR;
		evt.data.rx.len = len;
		tlsr8269_uart_event(dev, &evt);
	}

	if (data->async_rx_buf == NULL) {
		return;
	}

	if (data->async_rx_next_buf != NULL) {
		data->async_rx_buf = data->async_rx_next_buf;
		data->async_rx_len = data->async_rx_next_len;
		data->async_rx_next_buf = NULL;
		data->async_rx_next_len = 0u;
	} else {
		evt.type = UART_RX_BUF_REQUEST;
		tlsr8269_uart_event(dev, &evt);
	}

	tlsr8269_uart_rx_arm(data->async_rx_buf, data->async_rx_len);
}

/*
 * DMA line: RX-ready (0x526[0]) and TX-done (0x526[1]). TX-done is always
 * acknowledged here so the line drops. RX-ready stays set until the packet is
 * consumed, except that with RX interrupts off and no async RX buffer the
 * packet is dropped, which keeps the line from re-firing.
 */
static void tlsr8269_uart_dma_isr(const void *arg)
{
	const struct device *dev = arg;
	struct tlsr8269_uart_data *data = dev->data;
	uint8_t mask = TLSR8269_REG_DMA_IRQ_MSK;

	if ((TLSR8269_REG_DMA_RX_RDY0 & TLSR8269_FLD_DMA_TX_DONE) != 0u) {
		TLSR8269_REG_DMA_RX_RDY0 = TLSR8269_FLD_DMA_TX_DONE;
		tlsr8269_uart_async_tx_isr(dev);
	}

	if ((TLSR8269_REG_DMA_RX_RDY0 & TLSR8269_FLD_DMA_RX_UART) != 0u) {
		if (data->async_rx_buf != NULL) {
			tlsr8269_uart_async_rx_isr(dev);
		} else if ((mask & TLSR8269_FLD_UART_IRQ_RX) == 0u) {
			data->rx_pos = 0u;
			TLSR8269_REG_DMA_RX_RDY0 = TLSR8269_FLD_DMA_RX_UART;
		}
	}

	if (data->callback != NULL) {
		data->callback(dev, data->callback_data);
	}
}

/* UART line: RX error. The flag is cleared, and async RX is reported as stopped. */
static void tlsr8269_uart_err_isr(const void *arg)
{
	const struct device *dev = arg;
	struct tlsr8269_uart_data *data = dev->data;
	struct uart_event evt;

	if ((TLSR8269_REG_UART_STATUS0 & TLSR8269_FLD_UART_STATUS0_RX_ERR) != 0u) {
		TLSR8269_REG_UART_STATUS0 |= TLSR8269_FLD_UART_CLR_ERR;

		if (data->async_rx_buf != NULL) {
			evt.type = UART_RX_STOPPED;
			evt.data.rx_stop.reason = UART_ERROR_FRAMING;
			evt.data.rx_stop.data.buf = data->async_rx_buf;
			evt.data.rx_stop.data.offset = 0u;
			evt.data.rx_stop.data.len = 0u;
			tlsr8269_uart_event(dev, &evt);
		}
	}

	if (data->callback != NULL) {
		data->callback(dev, data->callback_data);
	}
}

static int tlsr8269_uart_async_callback_set(const struct device *dev, uart_callback_t callback,
					    void *user_data)
{
	struct tlsr8269_uart_data *data = dev->data;

	data->async_callback = callback;
	data->async_user_data = user_data;

	return 0;
}

static int tlsr8269_uart_async_tx(const struct device *dev, const uint8_t *buf, size_t len,
				  int32_t timeout)
{
	struct tlsr8269_uart_data *data = dev->data;
	unsigned int key;

	ARG_UNUSED(timeout);

	if (buf == NULL || len == 0u) {
		return -EINVAL;
	}

	key = irq_lock();
	if (data->async_tx_buf != NULL || !tlsr8269_uart_tx_idle()) {
		irq_unlock(key);
		return -EBUSY;
	}

	data->async_tx_buf = buf;
	data->async_tx_len = len;
	data->async_tx_pos = 0u;

	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_TX, true);
	tlsr8269_uart_async_tx_isr(dev);
	irq_unlock(key);

	return 0;
}

static int tlsr8269_uart_async_tx_abort(const struct device *dev)
{
	struct tlsr8269_uart_data *data = dev->data;
	struct uart_event evt;
	unsigned int key = irq_lock();

	if (data->async_tx_buf == NULL) {
		irq_unlock(key);
		return -EFAULT;
	}

	evt.type = UART_TX_ABORTED;
	evt.data.tx.buf = data->async_tx_buf;
	evt.data.tx.len = data->async_tx_pos;

	data->async_tx_buf = NULL;
	data->async_tx_len = 0u;
	data->async_tx_pos = 0u;
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_TX, false);
	irq_unlock(key);

	tlsr8269_uart_event(dev, &evt);

	return 0;
}

static int tlsr8269_uart_async_rx_enable(const struct device *dev, uint8_t *buf, size_t len,
					 int32_t timeout)
{
	struct tlsr8269_uart_data *data = dev->data;
	unsigned int key;

	ARG_UNUSED(timeout);

	if (buf == NULL || len < TLSR8269_UART_RX_MIN_LEN || len > TLSR8269_UART_RX_MAX_LEN ||
	    !tlsr8269_dma_addr_ok((uintptr_t)buf)) {
		return -EINVAL;
	}

	key = irq_lock();
	if (data->async_rx_buf != NULL) {
		irq_unlock(key);
		return -EBUSY;
	}

	data->async_rx_buf = buf;
	data->async_rx_len = len;
	data->async_rx_next_buf = NULL;
	data->async_rx_next_len = 0u;

	TLSR8269_REG_DMA_RX_RDY0 = TLSR8269_FLD_DMA_RX_UART;
	tlsr8269_uart_rx_arm(buf, len);
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_RX, true);
	irq_unlock(key);

	return 0;
}

static int tlsr8269_uart_async_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	struct tlsr8269_uart_data *data = dev->data;
	unsigned int key;

	if (buf == NULL || len < TLSR8269_UART_RX_MIN_LEN || len > TLSR8269_UART_RX_MAX_LEN ||
	    !tlsr8269_dma_addr_ok((uintptr_t)buf)) {
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

static int tlsr8269_uart_async_rx_disable(const struct device *dev)
{
	struct tlsr8269_uart_data *data = dev->data;
	struct uart_event evt;
	uint8_t *buf;
	uint8_t *next;
	unsigned int key = irq_lock();

	if (data->async_rx_buf == NULL) {
		irq_unlock(key);
		return -EFAULT;
	}

	buf = data->async_rx_buf;
	next = data->async_rx_next_buf;

	data->async_rx_buf = NULL;
	data->async_rx_len = 0u;
	data->async_rx_next_buf = NULL;
	data->async_rx_next_len = 0u;
	tlsr8269_uart_irq_enable_dma(TLSR8269_FLD_UART_IRQ_RX, false);
	TLSR8269_REG_DMA_RX_RDY0 = TLSR8269_FLD_DMA_RX_UART;
	irq_unlock(key);

	evt.type = UART_RX_BUF_RELEASED;
	evt.data.rx_buf.buf = buf;
	tlsr8269_uart_event(dev, &evt);
	if (next != NULL) {
		evt.data.rx_buf.buf = next;
		tlsr8269_uart_event(dev, &evt);
	}

	evt.type = UART_RX_DISABLED;
	tlsr8269_uart_event(dev, &evt);

	return 0;
}

static int tlsr8269_uart_init(const struct device *dev)
{
	const struct tlsr8269_uart_config *config = dev->config;
	struct tlsr8269_uart_data *data = dev->data;
	int ret;

	data->rx_pos = 0u;
	data->callback = NULL;
	data->callback_data = NULL;
	data->async_callback = NULL;
	data->async_user_data = NULL;
	data->async_tx_buf = NULL;
	data->async_tx_len = 0u;
	data->async_tx_pos = 0u;
	data->async_rx_buf = NULL;
	data->async_rx_len = 0u;
	data->async_rx_next_buf = NULL;
	data->async_rx_next_len = 0u;
	memset(data->rx_buf, 0, sizeof(data->rx_buf));

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	ret = tlsr8269_uart_hw_init(config, data);
	if (ret < 0) {
		return ret;
	}

	config->irq_config_func(dev);

	return 0;
}

static DEVICE_API(uart, tlsr8269_uart_driver_api) = {
	.poll_in = tlsr8269_uart_poll_in,
	.poll_out = tlsr8269_uart_poll_out,
	.err_check = tlsr8269_uart_err_check,
	.fifo_fill = tlsr8269_uart_fifo_fill,
	.fifo_read = tlsr8269_uart_fifo_read,
	.irq_tx_enable = tlsr8269_uart_irq_tx_enable,
	.irq_tx_disable = tlsr8269_uart_irq_tx_disable,
	.irq_tx_ready = tlsr8269_uart_irq_tx_ready,
	.irq_tx_complete = tlsr8269_uart_irq_tx_complete,
	.irq_rx_enable = tlsr8269_uart_irq_rx_enable,
	.irq_rx_disable = tlsr8269_uart_irq_rx_disable,
	.irq_rx_ready = tlsr8269_uart_irq_rx_ready,
	.irq_err_enable = tlsr8269_uart_irq_err_enable,
	.irq_err_disable = tlsr8269_uart_irq_err_disable,
	.irq_is_pending = tlsr8269_uart_irq_is_pending,
	.irq_update = tlsr8269_uart_irq_update,
	.irq_callback_set = tlsr8269_uart_irq_callback_set,
#ifdef CONFIG_UART_ASYNC_API
	.callback_set = tlsr8269_uart_async_callback_set,
	.tx = tlsr8269_uart_async_tx,
	.tx_abort = tlsr8269_uart_async_tx_abort,
	.rx_enable = tlsr8269_uart_async_rx_enable,
	.rx_buf_rsp = tlsr8269_uart_async_rx_buf_rsp,
	.rx_disable = tlsr8269_uart_async_rx_disable,
#endif
};

#define TLSR8269_UART_INIT(n)                                                                     \
	PINCTRL_DT_INST_DEFINE(n);                                                               \
	static void tlsr8269_uart_irq_config_##n(const struct device *dev)                       \
	{                                                                                         \
		IRQ_CONNECT(DT_INST_IRQN(n), 0, tlsr8269_uart_dma_isr, DEVICE_DT_INST_GET(n), 0); \
		IRQ_CONNECT(TLSR82XX_IRQ_UART, 0, tlsr8269_uart_err_isr, DEVICE_DT_INST_GET(n), 0); \
		ARG_UNUSED(dev);                                                                  \
	}                                                                                         \
	static struct tlsr8269_uart_data tlsr8269_uart_data_##n;                                 \
	static const struct tlsr8269_uart_config tlsr8269_uart_config_##n = {                    \
		.clock_frequency = DT_PROP(DT_INST_CLOCKS_CTLR(n), clock_frequency),        \
		.current_speed = DT_INST_PROP(n, current_speed),                                 \
		.irq_config_func = tlsr8269_uart_irq_config_##n,                                 \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                       \
	};                                                                                        \
	DEVICE_DT_INST_DEFINE(n, tlsr8269_uart_init, NULL, &tlsr8269_uart_data_##n,              \
			      &tlsr8269_uart_config_##n, PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, \
			      &tlsr8269_uart_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8269_UART_INIT)
