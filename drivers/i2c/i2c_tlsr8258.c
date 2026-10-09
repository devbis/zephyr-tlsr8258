/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Polled controller-mode driver for the TLSR8258 hardware I2C block, plus a
 * target (slave) mode built on the hardware mapping buffer.
 *
 * Register sequences follow the vendor SDK (i2c_master_init, i2c_read_series
 * and i2c_write_series in platform/lib/libdrivers_8258.a). Target mode follows
 * the vendor i2c_slave_init (mapping mode, ADDR_AUTO_ADD) disassembly.
 *
 * Target mode, unverified on hardware:
 *  - The hardware reports no received-byte count and no STOP condition, so
 *    write_received, buf_write_received, read_processed, buf_read_requested,
 *    stop and error are never invoked. Only write_requested and read_requested
 *    are dispatched.
 *  - Status events are dispatched from a work item polled every millisecond,
 *    not from the ISR (the ISR only latches and clears the status bits).
 *    Read data from read_requested is therefore late for the current read.
 *  - ACK/NAK, address match and clock stretching are not modelled.
 */

#define DT_DRV_COMPAT telink_tlsr8258_i2c

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <tlsr82xx/irq.h>

#define TLSR8258_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR8258_REG_CLK_EN0 TLSR8258_REG8(0x00800063u)
#define TLSR8258_REG_SPI_SP  TLSR8258_REG8(0x0080000au)

#define TLSR8258_REG_I2C_SPEED TLSR8258_REG8(0x00800000u)
#define TLSR8258_REG_I2C_ID    TLSR8258_REG8(0x00800001u)
#define TLSR8258_REG_I2C_STATUS TLSR8258_REG8(0x00800002u)
#define TLSR8258_REG_I2C_MODE  TLSR8258_REG8(0x00800003u)
#define TLSR8258_REG_I2C_DI    TLSR8258_REG8(0x00800006u)
#define TLSR8258_REG_I2C_CTRL  TLSR8258_REG8(0x00800007u)

/* Target (slave) mapping buffer address and event status, vendor reg_i2c_slave_*. */
#define TLSR8258_REG_I2C_SLAVE_MAP_ADDRL TLSR8258_REG8(0x008000e1u)
#define TLSR8258_REG_I2C_SLAVE_MAP_ADDRM TLSR8258_REG8(0x008000e2u)
#define TLSR8258_REG_I2C_SLAVE_MAP_ADDRH TLSR8258_REG8(0x008000e3u)
#define TLSR8258_REG_I2C_SLAVE_IRQ       TLSR8258_REG8(0x008000e4u)

#define TLSR8258_FLD_CLK0_I2C_EN BIT(1)
#define TLSR8258_FLD_SPI_ENABLE  BIT(7)

#define TLSR8258_FLD_I2C_WRITE_READ_BIT BIT(0)
#define TLSR8258_FLD_I2C_MASTER_EN      BIT(1)
#define TLSR8258_FLD_I2C_SLAVE_AUTO_ADD BIT(0)
#define TLSR8258_FLD_I2C_MAP_EN         BIT(2)

/* Slave IRQ status bits; HOST_CMD fires on host write and read, HOST_READ on read only. */
#define TLSR8258_FLD_I2C_SLAVE_HOST_CMD  BIT(0)
#define TLSR8258_FLD_I2C_SLAVE_HOST_READ BIT(1)

/* Mapping buffer: host writes land at offset 0, host reads come from offset 64. */
#define TLSR8258_I2C_TARGET_BUF_SIZE    128u
#define TLSR8258_I2C_TARGET_READ_OFFSET 64u
#define TLSR8258_I2C_TARGET_MAP_ADDRH   0x04u
#define TLSR8258_I2C_TARGET_POLL_MS     1u

#define TLSR8258_FLD_I2C_CMD_BUSY BIT(0)
#define TLSR8258_FLD_I2C_NAK      BIT(2)

#define TLSR8258_FLD_I2C_CMD_ID      BIT(0)
#define TLSR8258_FLD_I2C_CMD_DI      BIT(3)
#define TLSR8258_FLD_I2C_CMD_START   BIT(4)
#define TLSR8258_FLD_I2C_CMD_STOP    BIT(5)
#define TLSR8258_FLD_I2C_CMD_READ_ID BIT(6)
#define TLSR8258_FLD_I2C_CMD_ACK     BIT(7)

#define TLSR8258_I2C_SPEED_STANDARD_HZ 100000u
#define TLSR8258_I2C_SPEED_FAST_HZ     400000u

/* Upper bound on busy-wait polls of the command-busy flag (roughly tens of ms). */
#define TLSR8258_I2C_POLL_LIMIT 100000u

struct tlsr8258_i2c_config {
	uint32_t src_clock_frequency;
	uint32_t bus_frequency;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config_func)(const struct device *dev);
};

struct tlsr8258_i2c_data {
	struct k_mutex lock;
	uint32_t dev_config;
	/* Target mode state; target_cfg is protected by lock. */
	struct i2c_target_config *target_cfg;
	struct k_work_delayable target_work;
	/* Written by the ISR, consumed under irq_lock by the work item. */
	volatile uint8_t target_pending;
	uint8_t target_buf[TLSR8258_I2C_TARGET_BUF_SIZE];
};

static int tlsr8258_i2c_wait_idle(void)
{
	for (uint32_t i = 0; i < TLSR8258_I2C_POLL_LIMIT; i++) {
		if ((TLSR8258_REG_I2C_STATUS & TLSR8258_FLD_I2C_CMD_BUSY) == 0u) {
			return 0;
		}
	}

	return -ETIMEDOUT;
}

static int tlsr8258_i2c_command(uint8_t cmd)
{
	TLSR8258_REG_I2C_CTRL = cmd;

	return tlsr8258_i2c_wait_idle();
}

/* Send START followed by the 7-bit address and the R/W bit. */
static int tlsr8258_i2c_start(uint16_t addr, bool read)
{
	int ret;

	TLSR8258_REG_I2C_ID = (uint8_t)((addr << 1) | (read ? TLSR8258_FLD_I2C_WRITE_READ_BIT : 0u));

	ret = tlsr8258_i2c_command(TLSR8258_FLD_I2C_CMD_ID | TLSR8258_FLD_I2C_CMD_START);
	if (ret < 0) {
		return ret;
	}

	if ((TLSR8258_REG_I2C_STATUS & TLSR8258_FLD_I2C_NAK) != 0u) {
		return -EIO;
	}

	return 0;
}

static int tlsr8258_i2c_write_bytes(const uint8_t *buf, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		int ret;

		TLSR8258_REG_I2C_DI = buf[i];
		ret = tlsr8258_i2c_command(TLSR8258_FLD_I2C_CMD_DI);
		if (ret < 0) {
			return ret;
		}

		if ((TLSR8258_REG_I2C_STATUS & TLSR8258_FLD_I2C_NAK) != 0u) {
			return -EIO;
		}
	}

	return 0;
}

/*
 * ACK bit is set on the last byte only when the read ends the transfer, as the
 * vendor does before STOP. A read followed by a repeated start keeps ACK low.
 */
static int tlsr8258_i2c_read_bytes(uint8_t *buf, uint32_t len, bool ends_transfer)
{
	for (uint32_t i = 0; i < len; i++) {
		uint8_t cmd = TLSR8258_FLD_I2C_CMD_DI | TLSR8258_FLD_I2C_CMD_READ_ID;
		int ret;

		if (ends_transfer && (i == len - 1u)) {
			cmd |= TLSR8258_FLD_I2C_CMD_ACK;
		}

		ret = tlsr8258_i2c_command(cmd);
		if (ret < 0) {
			return ret;
		}

		buf[i] = TLSR8258_REG_I2C_DI;
	}

	return 0;
}

static int tlsr8258_i2c_configure(const struct device *dev, uint32_t dev_config)
{
	const struct tlsr8258_i2c_config *cfg = dev->config;
	struct tlsr8258_i2c_data *data = dev->data;
	uint32_t speed;
	uint32_t div;

	if ((dev_config & I2C_MODE_CONTROLLER) == 0u) {
		return -ENOTSUP;
	}

	if ((dev_config & I2C_ADDR_10_BITS) != 0u) {
		return -ENOTSUP;
	}

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		speed = TLSR8258_I2C_SPEED_STANDARD_HZ;
		break;
	case I2C_SPEED_FAST:
		speed = TLSR8258_I2C_SPEED_FAST_HZ;
		break;
	default:
		return -ENOTSUP;
	}

	/* I2C clock = system clock / (4 * divider), see vendor i2c_master_init. */
	div = cfg->src_clock_frequency / (4u * speed);
	if ((div == 0u) || (div > UINT8_MAX)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	TLSR8258_REG_I2C_SPEED = (uint8_t)div;
	data->dev_config = dev_config;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int tlsr8258_i2c_get_config(const struct device *dev, uint32_t *dev_config)
{
	struct tlsr8258_i2c_data *data = dev->data;

	if (dev_config == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	*dev_config = data->dev_config;
	k_mutex_unlock(&data->lock);

	return 0;
}

/*
 * Each message that starts a new bus transaction issues START. A message
 * continues the current transaction, with no new START, when the previous one
 * had no STOP, its direction matches and I2C_MSG_RESTART is not set.
 */
static int tlsr8258_i2c_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
				 uint16_t addr)
{
	struct tlsr8258_i2c_data *data = dev->data;
	bool in_transfer = false;
	bool cur_read = false;
	int ret = 0;

	if (num_msgs == 0u) {
		return 0;
	}

	if ((addr & ~0x7fu) != 0u) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->target_cfg != NULL) {
		/* The block is in target mode and cannot drive the bus. */
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	for (uint8_t i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];
		bool read = (msg->flags & I2C_MSG_RW_MASK) == I2C_MSG_READ;
		bool ends = (i == (num_msgs - 1u)) || ((msg->flags & I2C_MSG_STOP) != 0u);

		if ((msg->flags & I2C_MSG_ADDR_10_BITS) != 0u) {
			ret = -ENOTSUP;
			break;
		}

		if (!in_transfer || (read != cur_read) || ((msg->flags & I2C_MSG_RESTART) != 0u)) {
			in_transfer = true;
			cur_read = read;
			ret = tlsr8258_i2c_start(addr, read);
			if (ret < 0) {
				break;
			}
		}

		if (read) {
			ret = tlsr8258_i2c_read_bytes(msg->buf, msg->len, ends);
		} else {
			ret = tlsr8258_i2c_write_bytes(msg->buf, msg->len);
		}
		if (ret < 0) {
			break;
		}

		if (ends) {
			ret = tlsr8258_i2c_command(TLSR8258_FLD_I2C_CMD_STOP);
			in_transfer = false;
			if (ret < 0) {
				break;
			}
		}
	}

	if ((ret < 0) && in_transfer) {
		/* Release the bus after a failed START or data phase. */
		(void)tlsr8258_i2c_command(TLSR8258_FLD_I2C_CMD_STOP);
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

/*
 * Only latches and clears the slave status bits. It makes no calls, so it
 * does not run flash or other non-RAM code. Unverified: whether the level
 * IRQ re-asserts while the status bit is still set after the clear.
 */
static void tlsr8258_i2c_target_isr(const void *arg)
{
	const struct device *dev = arg;
	struct tlsr8258_i2c_data *data = dev->data;
	uint8_t status = TLSR8258_REG_I2C_SLAVE_IRQ &
			 (TLSR8258_FLD_I2C_SLAVE_HOST_CMD | TLSR8258_FLD_I2C_SLAVE_HOST_READ);

	if (status == 0u) {
		return;
	}

	TLSR8258_REG_I2C_SLAVE_IRQ = status;
	data->target_pending |= status;
}

/*
 * Runs on the system workqueue. Dispatches write_requested for host writes and
 * read_requested for host reads. The read byte is stored at offset 64 of the
 * mapping buffer, which is too late for the byte the host is already clocking.
 */
static void tlsr8258_i2c_target_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct tlsr8258_i2c_data *data =
		CONTAINER_OF(dwork, struct tlsr8258_i2c_data, target_work);
	struct i2c_target_config *cfg;
	uint8_t pending;
	unsigned int key;

	k_mutex_lock(&data->lock, K_FOREVER);

	cfg = data->target_cfg;
	if (cfg == NULL) {
		k_mutex_unlock(&data->lock);
		return;
	}

	key = irq_lock();
	pending = data->target_pending;
	data->target_pending = 0u;
	irq_unlock(key);

	if (((pending & TLSR8258_FLD_I2C_SLAVE_HOST_CMD) != 0u) && (cfg->callbacks != NULL)) {
		if ((pending & TLSR8258_FLD_I2C_SLAVE_HOST_READ) != 0u) {
			if (cfg->callbacks->read_requested != NULL) {
				uint8_t val = 0u;

				if (cfg->callbacks->read_requested(cfg, &val) == 0) {
					data->target_buf[TLSR8258_I2C_TARGET_READ_OFFSET] = val;
				}
			}
		} else if (cfg->callbacks->write_requested != NULL) {
			(void)cfg->callbacks->write_requested(cfg);
		}
	}

	(void)k_work_reschedule(&data->target_work, K_MSEC(TLSR8258_I2C_TARGET_POLL_MS));

	k_mutex_unlock(&data->lock);
}

/* Follows vendor i2c_slave_init: ID, clock enable, SPI_SP, mode, then mapping. */
static int tlsr8258_i2c_target_register(const struct device *dev, struct i2c_target_config *cfg)
{
	struct tlsr8258_i2c_data *data = dev->data;
	uint32_t map_addr;

	if ((cfg == NULL) || (cfg->callbacks == NULL)) {
		return -EINVAL;
	}

	if ((cfg->flags & I2C_TARGET_FLAGS_ADDR_10_BITS) != 0u) {
		return -ENOTSUP;
	}

	if ((cfg->address & ~0x7fu) != 0u) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->target_cfg != NULL) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	/* Device ID is address << 1; the LSB is the R/W bit and is ignored by the slave. */
	TLSR8258_REG_I2C_ID = (uint8_t)(cfg->address << 1);
	TLSR8258_REG_CLK_EN0 |= TLSR8258_FLD_CLK0_I2C_EN;
	TLSR8258_REG_SPI_SP &= (uint8_t)~TLSR8258_FLD_SPI_ENABLE;
	TLSR8258_REG_I2C_MODE &= (uint8_t)~TLSR8258_FLD_I2C_MASTER_EN;
	TLSR8258_REG_I2C_MODE |= TLSR8258_FLD_I2C_SLAVE_AUTO_ADD;

	/* Vendor mapping setup: MAP_EN first, then the buffer address, then the high byte. */
	map_addr = (uint32_t)(uintptr_t)data->target_buf;
	TLSR8258_REG_I2C_MODE |= TLSR8258_FLD_I2C_MAP_EN;
	TLSR8258_REG_I2C_SLAVE_MAP_ADDRL = (uint8_t)(map_addr & 0xffu);
	TLSR8258_REG_I2C_SLAVE_MAP_ADDRM = (uint8_t)((map_addr >> 8) & 0xffu);
	TLSR8258_REG_I2C_SLAVE_MAP_ADDRH = TLSR8258_I2C_TARGET_MAP_ADDRH;

	TLSR8258_REG_I2C_SLAVE_IRQ = TLSR8258_FLD_I2C_SLAVE_HOST_CMD |
				     TLSR8258_FLD_I2C_SLAVE_HOST_READ;
	data->target_pending = 0u;
	data->target_cfg = cfg;

	irq_enable(TLSR82XX_IRQ_CMD);
	(void)k_work_reschedule(&data->target_work, K_MSEC(TLSR8258_I2C_TARGET_POLL_MS));

	k_mutex_unlock(&data->lock);

	return 0;
}

static int tlsr8258_i2c_target_unregister(const struct device *dev, struct i2c_target_config *cfg)
{
	struct tlsr8258_i2c_data *data = dev->data;
	struct k_work_sync sync;

	if (cfg == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->target_cfg != cfg) {
		k_mutex_unlock(&data->lock);
		return -EINVAL;
	}

	irq_disable(TLSR82XX_IRQ_CMD);
	TLSR8258_REG_I2C_MODE &= (uint8_t)~(TLSR8258_FLD_I2C_MAP_EN |
					    TLSR8258_FLD_I2C_SLAVE_AUTO_ADD);
	TLSR8258_REG_I2C_MODE |= TLSR8258_FLD_I2C_MASTER_EN;
	data->target_cfg = NULL;

	k_mutex_unlock(&data->lock);

	/* Cleared under lock, so the work item cannot reschedule after this. */
	(void)k_work_cancel_delayable_sync(&data->target_work, &sync);

	return 0;
}

static int tlsr8258_i2c_init(const struct device *dev)
{
	const struct tlsr8258_i2c_config *cfg = dev->config;
	struct tlsr8258_i2c_data *data = dev->data;
	uint32_t speed_cfg;
	int ret;

	k_mutex_init(&data->lock);
	k_work_init_delayable(&data->target_work, tlsr8258_i2c_target_work);

	TLSR8258_REG_CLK_EN0 |= TLSR8258_FLD_CLK0_I2C_EN;
	TLSR8258_REG_SPI_SP &= (uint8_t)~TLSR8258_FLD_SPI_ENABLE;
	TLSR8258_REG_I2C_MODE |= TLSR8258_FLD_I2C_MASTER_EN;

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	cfg->irq_config_func(dev);

	speed_cfg = (cfg->bus_frequency >= TLSR8258_I2C_SPEED_FAST_HZ) ? I2C_SPEED_FAST
								       : I2C_SPEED_STANDARD;

	return tlsr8258_i2c_configure(dev, I2C_MODE_CONTROLLER | I2C_SPEED_SET(speed_cfg));
}

static DEVICE_API(i2c, tlsr8258_i2c_api) = {
	.configure = tlsr8258_i2c_configure,
	.get_config = tlsr8258_i2c_get_config,
	.transfer = tlsr8258_i2c_transfer,
	.target_register = IS_ENABLED(CONFIG_I2C_TARGET) ? tlsr8258_i2c_target_register : NULL,
	.target_unregister = IS_ENABLED(CONFIG_I2C_TARGET) ? tlsr8258_i2c_target_unregister : NULL,
};

#define TLSR8258_I2C_INIT(n)                                                                       \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static void tlsr8258_i2c_irq_config_##n(const struct device *dev)                          \
	{                                                                                          \
		ARG_UNUSED(dev);                                                                   \
		IRQ_CONNECT(TLSR82XX_IRQ_CMD, 0, tlsr8258_i2c_target_isr, DEVICE_DT_INST_GET(n),   \
			    0);                                                                    \
	}                                                                                          \
	static struct tlsr8258_i2c_data tlsr8258_i2c_data_##n;                                     \
	static const struct tlsr8258_i2c_config tlsr8258_i2c_config_##n = {                        \
		.src_clock_frequency = DT_PROP(DT_INST_CLOCKS_CTLR(n), clock_frequency),           \
		.bus_frequency = DT_INST_PROP_OR(n, clock_frequency, TLSR8258_I2C_SPEED_STANDARD_HZ), \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.irq_config_func = tlsr8258_i2c_irq_config_##n,                                    \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, tlsr8258_i2c_init, NULL, &tlsr8258_i2c_data_##n,                  \
			      &tlsr8258_i2c_config_##n, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,     \
			      &tlsr8258_i2c_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_I2C_INIT)
