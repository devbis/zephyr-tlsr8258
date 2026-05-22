/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_flash

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/arch/tc32/irq.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define TLSR8258_FLASH_PAGE_SIZE   256u
#define TLSR8258_FLASH_SECTOR_SIZE 4096u

#define TLSR8258_FLASH_CMD_WRITE_ENABLE 0x06u
#define TLSR8258_FLASH_CMD_READ_STATUS  0x05u
#define TLSR8258_FLASH_CMD_PAGE_PROGRAM 0x02u
#define TLSR8258_FLASH_CMD_SECTOR_ERASE 0x20u

#define TLSR8258_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR8258_REG_MSPI_DATA TLSR8258_REG8(0x0080000cu)
#define TLSR8258_REG_MSPI_CTRL TLSR8258_REG8(0x0080000du)
#define TLSR8258_REG_MSPI_MODE TLSR8258_REG8(0x0080000fu)
#define TLSR8258_REG_TMR_CTRL  (*(volatile uint32_t *)0x00800620u)
#define TLSR8258_REG_SYSTEM_TICK (*(volatile uint32_t *)0x00800740u)
#define TLSR8258_REG_ANA_ADDR TLSR8258_REG8(0x008000b8u)
#define TLSR8258_REG_ANA_DATA TLSR8258_REG8(0x008000b9u)
#define TLSR8258_REG_ANA_CTRL TLSR8258_REG8(0x008000bau)

#define TLSR8258_FLD_MSPI_CS   BIT(0)
#define TLSR8258_FLD_MSPI_RD   BIT(3)
#define TLSR8258_FLD_MSPI_BUSY BIT(4)
#define TLSR8258_FLD_MSPI_DUAL_DATA_MODE_EN BIT(0)
#define TLSR8258_FLD_MSPI_DUAL_ADDR_MODE_EN BIT(1)
#define TLSR8258_FLD_ANA_BUSY BIT(0)
#define TLSR8258_FLD_ANA_RW   BIT(5)
#define TLSR8258_FLD_ANA_CYC0 BIT(6)
#define TLSR8258_FLD_CLR_WD    BIT(27)
#define TLSR8258_SYS_TICKS_PER_US 16u

#define TLSR8258_AREG_FLASH_VOLTAGE 0x0cu

#define TLSR8258_FLASH_CFG_VDD_F_64K   0x00e1c0u
#define TLSR8258_FLASH_CFG_VDD_F_128K  0x01e1c0u
#define TLSR8258_FLASH_CFG_VDD_F_512K  0x0771c0u
#define TLSR8258_FLASH_CFG_VDD_F_1M    0x0fe1c0u
#define TLSR8258_FLASH_CFG_VDD_F_2M    0x1fe1c0u

#define TLSR8258_RAM_CODE __attribute__((noinline, section(".ram_code")))

struct tlsr8258_flash_config {
	uintptr_t base;
	size_t size;
};

struct tlsr8258_flash_data {
	struct k_sem lock;
	struct flash_pages_layout layout;
};

static const struct flash_parameters tlsr8258_flash_parameters = {
	.write_block_size = 1,
	.erase_value = 0xff,
};

static ALWAYS_INLINE void tlsr8258_mspi_wait(void)
{
	while ((TLSR8258_REG_MSPI_CTRL & TLSR8258_FLD_MSPI_BUSY) != 0u) {
	}
}

static ALWAYS_INLINE void tlsr8258_mspi_high(void)
{
	TLSR8258_REG_MSPI_CTRL = TLSR8258_FLD_MSPI_CS;
}

static ALWAYS_INLINE void tlsr8258_mspi_low(void)
{
	TLSR8258_REG_MSPI_CTRL = 0u;
}

static ALWAYS_INLINE void tlsr8258_mspi_write(uint8_t value)
{
	TLSR8258_REG_MSPI_DATA = value;
}

static ALWAYS_INLINE uint8_t tlsr8258_mspi_get(void)
{
	return TLSR8258_REG_MSPI_DATA;
}

static ALWAYS_INLINE uint8_t tlsr8258_mspi_mode_manual(uint8_t mode)
{
	return mode & ~(TLSR8258_FLD_MSPI_DUAL_DATA_MODE_EN |
			TLSR8258_FLD_MSPI_DUAL_ADDR_MODE_EN);
}

static ALWAYS_INLINE uint8_t tlsr8258_mspi_read(void)
{
	tlsr8258_mspi_write(0u);
	tlsr8258_mspi_wait();
	return tlsr8258_mspi_get();
}

static ALWAYS_INLINE void tlsr8258_watchdog_clear(void)
{
	TLSR8258_REG_TMR_CTRL |= TLSR8258_FLD_CLR_WD;
}

static ALWAYS_INLINE void tlsr8258_analog_wait(void)
{
	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
	}
}

static uint8_t tlsr8258_analog_read(uint8_t addr)
{
	unsigned int key = arch_irq_lock();
	uint8_t value;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	tlsr8258_analog_wait();
	value = TLSR8258_REG_ANA_DATA;
	TLSR8258_REG_ANA_CTRL = 0u;
	arch_irq_unlock(key);

	return value;
}

static void tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = arch_irq_lock();

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	tlsr8258_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
	arch_irq_unlock(key);
}

static bool tlsr8258_flash_vdd_calib_offset(size_t flash_size, uint32_t *offset)
{
	switch (flash_size) {
	case 64u * 1024u:
		*offset = TLSR8258_FLASH_CFG_VDD_F_64K;
		return true;
	case 128u * 1024u:
		*offset = TLSR8258_FLASH_CFG_VDD_F_128K;
		return true;
	case 512u * 1024u:
		*offset = TLSR8258_FLASH_CFG_VDD_F_512K;
		return true;
	case 1024u * 1024u:
		*offset = TLSR8258_FLASH_CFG_VDD_F_1M;
		return true;
	case 2u * 1024u * 1024u:
		*offset = TLSR8258_FLASH_CFG_VDD_F_2M;
		return true;
	default:
		return false;
	}
}

static void tlsr8258_flash_apply_vdd_calibration(const struct tlsr8258_flash_config *config)
{
	uint32_t calib_offset;
	uint8_t calib_value;
	uint8_t reg_value;

	/*
	 * Vendor startup trims ana_0x0c before any flash program/erase. Without
	 * this, the first NVS page program on 512K boards can reset the chip.
	 */
	if (!tlsr8258_flash_vdd_calib_offset(config->size, &calib_offset) ||
	    calib_offset >= config->size) {
		return;
	}

	calib_value = *(const volatile uint8_t *)(config->base + calib_offset);
	if ((calib_value == 0xffu) || ((calib_value & 0xf8u) != 0u)) {
		return;
	}

	reg_value = tlsr8258_analog_read(TLSR8258_AREG_FLASH_VOLTAGE);
	tlsr8258_analog_write(TLSR8258_AREG_FLASH_VOLTAGE,
			      (reg_value & 0xf8u) | (calib_value & 0x07u));
}

static TLSR8258_RAM_CODE void tlsr8258_flash_sleep_us(uint32_t us)
{
	uint32_t start = TLSR8258_REG_SYSTEM_TICK;
	uint32_t ticks = us * TLSR8258_SYS_TICKS_PER_US;

	while ((uint32_t)(TLSR8258_REG_SYSTEM_TICK - start) <= ticks) {
	}
}

static TLSR8258_RAM_CODE void tlsr8258_flash_send_cmd(uint8_t cmd)
{
	tlsr8258_mspi_high();
	tlsr8258_flash_sleep_us(1u);
	tlsr8258_mspi_low();
	tlsr8258_mspi_write(cmd);
	tlsr8258_mspi_wait();
}

static TLSR8258_RAM_CODE void tlsr8258_flash_send_addr(uint32_t addr)
{
	tlsr8258_mspi_write((uint8_t)(addr >> 16));
	tlsr8258_mspi_wait();
	tlsr8258_mspi_write((uint8_t)(addr >> 8));
	tlsr8258_mspi_wait();
	tlsr8258_mspi_write((uint8_t)addr);
	tlsr8258_mspi_wait();
}

static TLSR8258_RAM_CODE int tlsr8258_flash_wait_done(void)
{
	tlsr8258_flash_sleep_us(100u);
	tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_READ_STATUS);

	for (uint32_t i = 0; i < 10000000u; i++) {
		if ((tlsr8258_mspi_read() & BIT(0)) == 0u) {
			tlsr8258_mspi_high();
			tlsr8258_flash_sleep_us(1u);
			return 0;
		}
	}

	tlsr8258_mspi_high();
	tlsr8258_flash_sleep_us(1u);
	return -ETIMEDOUT;
}

int tlsr8258_flash_write_page_ram(uint32_t addr, const uint8_t *buf, size_t len);
int tlsr8258_flash_erase_sector_ram(uint32_t addr);
int tlsr8258_flash_read_ram(uint32_t addr, uint8_t *buf, size_t len);

TLSR8258_RAM_CODE int tlsr8258_flash_read_ram_body(uint32_t addr, uint8_t *buf, size_t len)
{
	uint8_t saved_mode = TLSR8258_REG_MSPI_MODE;

	TLSR8258_REG_MSPI_MODE = tlsr8258_mspi_mode_manual(saved_mode);
	tlsr8258_flash_send_cmd(0x03u);
	tlsr8258_flash_send_addr(addr);
	tlsr8258_mspi_write(0u);
	tlsr8258_mspi_wait();
	TLSR8258_REG_MSPI_CTRL = 0x0au;
	tlsr8258_mspi_wait();

	for (size_t i = 0; i < len; i++) {
		buf[i] = tlsr8258_mspi_get();
		tlsr8258_mspi_wait();
	}

	tlsr8258_mspi_high();
	TLSR8258_REG_MSPI_MODE = saved_mode;
	return 0;
}

TLSR8258_RAM_CODE int tlsr8258_flash_write_page_ram_body(uint32_t addr, const uint8_t *buf,
							 size_t len)
{
	uint8_t saved_mode = TLSR8258_REG_MSPI_MODE;
	int ret;

	TLSR8258_REG_MSPI_MODE = tlsr8258_mspi_mode_manual(saved_mode);
	tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_WRITE_ENABLE);
	tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_PAGE_PROGRAM);
	tlsr8258_flash_send_addr(addr);

	for (size_t i = 0; i < len; i++) {
		tlsr8258_mspi_write(buf[i]);
		tlsr8258_mspi_wait();
	}

	tlsr8258_mspi_high();
	ret = tlsr8258_flash_wait_done();
	TLSR8258_REG_MSPI_MODE = saved_mode;
	return ret;
}

TLSR8258_RAM_CODE int tlsr8258_flash_erase_sector_ram_body(uint32_t addr)
{
	uint8_t saved_mode = TLSR8258_REG_MSPI_MODE;
	int ret;

	TLSR8258_REG_MSPI_MODE = tlsr8258_mspi_mode_manual(saved_mode);
	tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_WRITE_ENABLE);
	tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_SECTOR_ERASE);
	tlsr8258_flash_send_addr(addr);
	tlsr8258_mspi_high();

	ret = tlsr8258_flash_wait_done();
	TLSR8258_REG_MSPI_MODE = saved_mode;
	return ret;
}

static bool tlsr8258_flash_range_valid(const struct tlsr8258_flash_config *config,
				       off_t offset, size_t len)
{
	return offset >= 0 && (size_t)offset <= config->size &&
	       len <= (config->size - (size_t)offset);
}

static int tlsr8258_flash_read(const struct device *dev, off_t offset, void *data,
			       size_t len)
{
	const struct tlsr8258_flash_config *config = dev->config;
	struct tlsr8258_flash_data *dev_data = dev->data;
	uint8_t *dst = data;
	int ret;

	if (!tlsr8258_flash_range_valid(config, offset, len)) {
		return -EINVAL;
	}

	if (len == 0u) {
		return 0;
	}

	k_sem_take(&dev_data->lock, K_FOREVER);
	{
		unsigned int key = arch_irq_lock();

		ret = tlsr8258_flash_read_ram((uint32_t)offset, dst, len);
		arch_irq_unlock(key);
	}
	k_sem_give(&dev_data->lock);

	return ret;
}

static int tlsr8258_flash_write(const struct device *dev, off_t offset,
				const void *data, size_t len)
{
	const struct tlsr8258_flash_config *config = dev->config;
	struct tlsr8258_flash_data *dev_data = dev->data;
	const uint8_t *src = data;
	uint8_t page_buf[TLSR8258_FLASH_PAGE_SIZE];
	int ret = 0;

	if (!tlsr8258_flash_range_valid(config, offset, len)) {
		return -EINVAL;
	}

	k_sem_take(&dev_data->lock, K_FOREVER);

	while (len > 0u) {
		size_t page_off = (size_t)offset & (TLSR8258_FLASH_PAGE_SIZE - 1u);
		size_t chunk = MIN(len, TLSR8258_FLASH_PAGE_SIZE - page_off);
		unsigned int key;

		memcpy(page_buf, src, chunk);
		key = arch_irq_lock();
		ret = tlsr8258_flash_write_page_ram((uint32_t)offset, page_buf, chunk);
		arch_irq_unlock(key);
		if (ret < 0) {
			break;
		}

		offset += chunk;
		src += chunk;
		len -= chunk;
	}

	k_sem_give(&dev_data->lock);
	return ret;
}

static int tlsr8258_flash_erase(const struct device *dev, off_t offset, size_t len)
{
	const struct tlsr8258_flash_config *config = dev->config;
	struct tlsr8258_flash_data *dev_data = dev->data;
	int ret = 0;

	if (!tlsr8258_flash_range_valid(config, offset, len) ||
	    !IS_ALIGNED((size_t)offset, TLSR8258_FLASH_SECTOR_SIZE) ||
	    !IS_ALIGNED(len, TLSR8258_FLASH_SECTOR_SIZE)) {
		return -EINVAL;
	}

	k_sem_take(&dev_data->lock, K_FOREVER);
	while (len > 0u) {
		unsigned int key;

		tlsr8258_watchdog_clear();
		key = arch_irq_lock();
		ret = tlsr8258_flash_erase_sector_ram((uint32_t)offset);
		arch_irq_unlock(key);
		if (ret < 0) {
			break;
		}

		offset += TLSR8258_FLASH_SECTOR_SIZE;
		len -= TLSR8258_FLASH_SECTOR_SIZE;
	}

	k_sem_give(&dev_data->lock);
	return ret;
}

static const struct flash_parameters *tlsr8258_flash_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &tlsr8258_flash_parameters;
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static void tlsr8258_flash_page_layout(const struct device *dev,
				       const struct flash_pages_layout **layout,
				       size_t *layout_size)
{
	struct tlsr8258_flash_data *data = dev->data;

	*layout = &data->layout;
	*layout_size = 1u;
}
#endif

static int tlsr8258_flash_init(const struct device *dev)
{
	const struct tlsr8258_flash_config *config = dev->config;
	struct tlsr8258_flash_data *data = dev->data;

	k_sem_init(&data->lock, 1, 1);
	data->layout.pages_count = config->size / TLSR8258_FLASH_SECTOR_SIZE;
	data->layout.pages_size = TLSR8258_FLASH_SECTOR_SIZE;
	tlsr8258_flash_apply_vdd_calibration(config);

	return 0;
}

static DEVICE_API(flash, tlsr8258_flash_api) = {
	.read = tlsr8258_flash_read,
	.write = tlsr8258_flash_write,
	.erase = tlsr8258_flash_erase,
	.get_parameters = tlsr8258_flash_get_parameters,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = tlsr8258_flash_page_layout,
#endif
};

#define TLSR8258_FLASH_INIT(n)							\
	static const struct tlsr8258_flash_config tlsr8258_flash_config_##n = {	\
		.base = DT_INST_REG_ADDR(n),					\
		.size = DT_INST_REG_SIZE(n),					\
	};									\
	static struct tlsr8258_flash_data tlsr8258_flash_data_##n;		\
										\
	DEVICE_DT_INST_DEFINE(n, tlsr8258_flash_init, NULL,			\
			      &tlsr8258_flash_data_##n,			\
			      &tlsr8258_flash_config_##n, POST_KERNEL,		\
			      CONFIG_FLASH_INIT_PRIORITY, &tlsr8258_flash_api);

DT_INST_FOREACH_STATUS_OKAY(TLSR8258_FLASH_INIT)
