/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
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

#include "flash_tlsr8258_paged_write.h"

#define TLSR8258_FLASH_PAGE_SIZE   256u
#define TLSR8258_FLASH_SECTOR_SIZE 4096u

#define TLSR8258_FLASH_CMD_WRITE_ENABLE 0x06u
#define TLSR8258_FLASH_CMD_READ_STATUS  0x05u
#define TLSR8258_FLASH_CMD_PAGE_PROGRAM 0x02u
#define TLSR8258_FLASH_CMD_SECTOR_ERASE 0x20u
#define TLSR8258_FLASH_ENTRY __attribute__((noinline, section(".ram_code")))
#define TLSR8258_FLASH_EXEC __attribute__((noinline, section(".ram_code")))

#define TLSR8258_REG8(addr) (*(volatile uint8_t *)(addr))

#define TLSR8258_REG_MSPI_DATA TLSR8258_REG8(0x0080000cu)
#define TLSR8258_REG_MSPI_CTRL TLSR8258_REG8(0x0080000du)
#define TLSR8258_REG_MSPI_MODE TLSR8258_REG8(0x0080000fu)
#define TLSR8258_REG_TMR_STA   TLSR8258_REG8(0x00800623u)
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
#define TLSR8258_FLD_TMR_STA_WD BIT(3)
#define TLSR8258_SYS_TICKS_PER_US 16u
#define TLSR8258_MSPI_WAIT_MAX_SPINS 1000000u
/* Bytes read per interrupt-masked transaction. */
#define TLSR8258_FLASH_READ_CHUNK 256u
/* Status polls before a program or erase is reported as timed out. */
#define TLSR8258_FLASH_WAIT_DONE_MAX_POLLS 10000000u
#define TLSR8258_FLASH_STATUS_WIP BIT(0)
#define TLSR8258_FLASH_CMD_READ 0x03u
/* MSPI_CTRL value that starts automatic read of one byte per data access. */
#define TLSR8258_MSPI_CTRL_AUTO_READ 0x0au

#define TLSR8258_AREG_FLASH_VOLTAGE 0x0cu

#define TLSR8258_FLASH_CFG_VDD_F_64K   0x00e1c0u
#define TLSR8258_FLASH_CFG_VDD_F_128K  0x01e1c0u
#define TLSR8258_FLASH_CFG_VDD_F_512K  0x0771c0u
#define TLSR8258_FLASH_CFG_VDD_F_1M    0x0fe1c0u
#define TLSR8258_FLASH_CFG_VDD_F_2M    0x1fe1c0u

struct tlsr8258_flash_config {
	uintptr_t base;
	size_t size;
};

struct tlsr8258_flash_data {
	struct k_sem lock;
	struct flash_pages_layout layout;
};

struct tlsr8258_flash_write_ctx {
	uint8_t *page_buf;
};

static const struct flash_parameters tlsr8258_flash_parameters = {
	.write_block_size = 1,
	.erase_value = 0xff,
};

static ALWAYS_INLINE int tlsr8258_mspi_wait(void)
{
	uint32_t spins = 0u;

	while ((TLSR8258_REG_MSPI_CTRL & TLSR8258_FLD_MSPI_BUSY) != 0u) {
		spins++;
		if (spins >= TLSR8258_MSPI_WAIT_MAX_SPINS) {
			return -ETIMEDOUT;
		}
	}
	return 0;
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

/*
 * In auto-read mode a read of MSPI_DATA returns the fetched byte and starts
 * the next transfer, so the register must only be read where a data byte is
 * consumed.
 */
static ALWAYS_INLINE uint8_t tlsr8258_mspi_get(void)
{
	return TLSR8258_REG_MSPI_DATA;
}

static ALWAYS_INLINE uint8_t tlsr8258_mspi_mode_manual(uint8_t mode)
{
	return mode & ~(TLSR8258_FLD_MSPI_DUAL_DATA_MODE_EN |
			TLSR8258_FLD_MSPI_DUAL_ADDR_MODE_EN);
}

static ALWAYS_INLINE uint8_t tlsr8258_flash_irq_disable(void)
{
	return (uint8_t)arch_irq_lock();
}

static ALWAYS_INLINE void tlsr8258_flash_irq_restore(uint8_t key)
{
	arch_irq_unlock(key);
}

static ALWAYS_INLINE void tlsr8258_watchdog_clear(void)
{
	/* reg_tmr_sta is write-one-to-clear: touch only the watchdog bit. */
	TLSR8258_REG_TMR_STA = TLSR8258_FLD_TMR_STA_WD;
}

static ALWAYS_INLINE int tlsr8258_analog_wait(void)
{
	uint32_t spins = 0u;

	while ((TLSR8258_REG_ANA_CTRL & TLSR8258_FLD_ANA_BUSY) != 0u) {
		spins++;
		if (spins >= TLSR8258_MSPI_WAIT_MAX_SPINS) {
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int tlsr8258_analog_read(uint8_t addr, uint8_t *value)
{
	unsigned int key = arch_irq_lock();
	int ret;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0;
	ret = tlsr8258_analog_wait();
	if (ret == 0) {
		*value = TLSR8258_REG_ANA_DATA;
	}
	TLSR8258_REG_ANA_CTRL = 0u;
	arch_irq_unlock(key);

	return ret;
}

static int tlsr8258_analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = arch_irq_lock();
	int ret;

	TLSR8258_REG_ANA_ADDR = addr;
	TLSR8258_REG_ANA_DATA = value;
	TLSR8258_REG_ANA_CTRL = TLSR8258_FLD_ANA_CYC0 | TLSR8258_FLD_ANA_RW;
	ret = tlsr8258_analog_wait();
	TLSR8258_REG_ANA_CTRL = 0u;
	arch_irq_unlock(key);

	return ret;
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

static int tlsr8258_flash_apply_vdd_calibration(const struct tlsr8258_flash_config *config)
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
		return 0;
	}

	calib_value = *(const volatile uint8_t *)(config->base + calib_offset);
	if ((calib_value == 0xffu) || ((calib_value & 0xf8u) != 0u)) {
		return 0;
	}

	if (tlsr8258_analog_read(TLSR8258_AREG_FLASH_VOLTAGE, &reg_value) != 0) {
		return -ETIMEDOUT;
	}

	return tlsr8258_analog_write(TLSR8258_AREG_FLASH_VOLTAGE,
				     (reg_value & 0xf8u) | (calib_value & 0x07u));
}

static TLSR8258_FLASH_EXEC void tlsr8258_flash_sleep_us(uint32_t us)
{
	for (volatile uint32_t delay = 0u;
	     delay < (us * TLSR8258_SYS_TICKS_PER_US);
	     delay++) {
	}
}

static TLSR8258_FLASH_EXEC int tlsr8258_flash_send_cmd(uint8_t cmd)
{
	tlsr8258_mspi_high();
	tlsr8258_flash_sleep_us(1u);
	tlsr8258_mspi_low();
	tlsr8258_mspi_write(cmd);
	if (tlsr8258_mspi_wait() != 0) {
		return -ETIMEDOUT;
	}
	return 0;
}

static TLSR8258_FLASH_EXEC int tlsr8258_flash_send_addr(uint32_t addr)
{
	tlsr8258_mspi_write((uint8_t)(addr >> 16));
	if (tlsr8258_mspi_wait() != 0) {
		return -ETIMEDOUT;
	}
	tlsr8258_mspi_write((uint8_t)(addr >> 8));
	if (tlsr8258_mspi_wait() != 0) {
		return -ETIMEDOUT;
	}
	tlsr8258_mspi_write((uint8_t)addr);
	if (tlsr8258_mspi_wait() != 0) {
		return -ETIMEDOUT;
	}
	return 0;
}

/*
 * Poll the status register until the program or erase completes. The caller
 * has interrupts masked for the whole operation: code executes in place from
 * this flash, so no handler outside .ram_code may run until it is idle again.
 * A sector erase therefore blocks interrupts for its full duration (tens of
 * milliseconds).
 */
static TLSR8258_FLASH_EXEC int tlsr8258_flash_wait_done(void)
{
	tlsr8258_flash_sleep_us(100u);
	if (tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_READ_STATUS) != 0) {
		tlsr8258_mspi_high();
		return -ETIMEDOUT;
	}

	for (uint32_t iter = 0; iter < TLSR8258_FLASH_WAIT_DONE_MAX_POLLS; iter++) {
		uint8_t status;

		tlsr8258_flash_sleep_us(1u);
		tlsr8258_mspi_write(0u);
		if (tlsr8258_mspi_wait() != 0) {
			tlsr8258_mspi_high();
			return -ETIMEDOUT;
		}
		status = tlsr8258_mspi_get();

		if ((iter & 0x3ffu) == 0u) {
			tlsr8258_watchdog_clear();
		}
		if ((status & TLSR8258_FLASH_STATUS_WIP) == 0u) {
			tlsr8258_mspi_high();
			tlsr8258_flash_sleep_us(1u);
			return 0;
		}
	}

	tlsr8258_mspi_high();
	tlsr8258_flash_sleep_us(1u);
	return -ETIMEDOUT;
}

/*
 * Runtime flash transactions must execute from RAM after XIP startup. The
 * TC32 boot mirror is reserved for reset/IRQ/suspend glue only.
 */
TLSR8258_FLASH_EXEC int tlsr8258_flash_read_ram(uint32_t addr, uint8_t *buf, size_t len)
{
	uint8_t key = tlsr8258_flash_irq_disable();
	uint8_t saved_mode = TLSR8258_REG_MSPI_MODE;
	int ret = 0;

	TLSR8258_REG_MSPI_MODE = tlsr8258_mspi_mode_manual(saved_mode);
	tlsr8258_mspi_high();
	/*
	 * This path is used by NVS before the Zephyr system timer is started.
	 * Do not wait on REG_SYSTEM_TICK here: on a cold boot it can remain at
	 * zero indefinitely and trap the CPU in this loop.  A short bounded
	 * CPU delay is sufficient for MSPI CS setup and is safe before clocks/
	 * interrupts are fully initialised.
	 */
	for (volatile uint32_t delay = 0u; delay < 8u; delay++) {
	}
	tlsr8258_mspi_low();
	tlsr8258_mspi_write(TLSR8258_FLASH_CMD_READ);
	if (tlsr8258_mspi_wait() != 0) {
		ret = -ETIMEDOUT;
		goto out;
	}

	tlsr8258_mspi_write((uint8_t)(addr >> 16));
	if (tlsr8258_mspi_wait() != 0) {
		ret = -ETIMEDOUT;
		goto out;
	}
	tlsr8258_mspi_write((uint8_t)(addr >> 8));
	if (tlsr8258_mspi_wait() != 0) {
		ret = -ETIMEDOUT;
		goto out;
	}
	tlsr8258_mspi_write((uint8_t)addr);
	if (tlsr8258_mspi_wait() != 0) {
		ret = -ETIMEDOUT;
		goto out;
	}

	tlsr8258_mspi_write(0u);
	if (tlsr8258_mspi_wait() != 0) {
		ret = -ETIMEDOUT;
		goto out;
	}
	TLSR8258_REG_MSPI_CTRL = TLSR8258_MSPI_CTRL_AUTO_READ;
	if (tlsr8258_mspi_wait() != 0) {
		ret = -ETIMEDOUT;
		goto out;
	}
	for (volatile uint32_t delay = 0u; delay < 8u; delay++) {
	}

	for (size_t i = 0; i < len; i++) {
		buf[i] = tlsr8258_mspi_get();
		if (tlsr8258_mspi_wait() != 0) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

out:
	tlsr8258_mspi_high();
	TLSR8258_REG_MSPI_MODE = saved_mode;
	tlsr8258_flash_irq_restore(key);
	return ret;
}

TLSR8258_FLASH_EXEC int tlsr8258_flash_write_page_ram(uint32_t addr, const uint8_t *buf,
						      size_t len)
{
	uint8_t saved_mode = TLSR8258_REG_MSPI_MODE;
	int ret;

	TLSR8258_REG_MSPI_MODE = tlsr8258_mspi_mode_manual(saved_mode);
	ret = tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_WRITE_ENABLE);
	if (ret != 0) {
		goto out;
	}
	ret = tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_PAGE_PROGRAM);
	if (ret != 0) {
		goto out;
	}
	ret = tlsr8258_flash_send_addr(addr);
	if (ret != 0) {
		goto out;
	}

	for (size_t i = 0; i < len; i++) {
		tlsr8258_mspi_write(buf[i]);
		ret = tlsr8258_mspi_wait();
		if (ret != 0) {
			goto out;
		}
	}

	tlsr8258_mspi_high();
	ret = tlsr8258_flash_wait_done();

out:
	tlsr8258_mspi_high();
	TLSR8258_REG_MSPI_MODE = saved_mode;
	return ret;
}

TLSR8258_FLASH_EXEC int tlsr8258_flash_erase_sector_ram(uint32_t addr)
{
	uint8_t saved_mode = TLSR8258_REG_MSPI_MODE;
	int ret;

	TLSR8258_REG_MSPI_MODE = tlsr8258_mspi_mode_manual(saved_mode);
	ret = tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_WRITE_ENABLE);
	if (ret != 0) {
		goto out;
	}
	ret = tlsr8258_flash_send_cmd(TLSR8258_FLASH_CMD_SECTOR_ERASE);
	if (ret != 0) {
		goto out;
	}
	ret = tlsr8258_flash_send_addr(addr);
	if (ret != 0) {
		goto out;
	}
	tlsr8258_mspi_high();

	ret = tlsr8258_flash_wait_done();

out:
	tlsr8258_mspi_high();
	TLSR8258_REG_MSPI_MODE = saved_mode;
	return ret;
}

static bool tlsr8258_flash_range_valid(const struct tlsr8258_flash_config *config,
				       off_t offset, size_t len)
{
	return offset >= 0 && (size_t)offset <= config->size &&
	       len <= (config->size - (size_t)offset);
}

static TLSR8258_FLASH_ENTRY int tlsr8258_flash_read(const struct device *dev,
							   off_t offset, void *data,
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

	/*
	 * Each chunk is one interrupt-masked transaction; interrupts are served
	 * between chunks so that a long read does not block the radio.
	 */
	k_sem_take(&dev_data->lock, K_FOREVER);
	ret = 0;
	while ((len > 0u) && (ret == 0)) {
		size_t chunk = MIN(len, (size_t)TLSR8258_FLASH_READ_CHUNK);

		ret = tlsr8258_flash_read_ram((uint32_t)offset, dst, chunk);
		offset += (off_t)chunk;
		dst += chunk;
		len -= chunk;
	}
	k_sem_give(&dev_data->lock);

	return ret;
}

/* Non-static wrapper called directly by tlsr8258_flash_write_pages
 * (see flash_tlsr8258_paged_write.c). The paged-write loop and this
 * wrapper both live in .ram_code, so the call site issues a short
 * intra-section branch; entry from flash .text into .ram_code is routed
 * through the `tlsr8258_flash_call_write_pages` / `_call_erase_sector_locked`
 * veneers in flash_tlsr8258_entry.S to avoid the linker-emitted
 * long-range thunk that wedges on the first XIP fetch after arch_irq_lock.
 */
TLSR8258_FLASH_EXEC void tlsr8258_flash_watchdog_clear(void)
{
	tlsr8258_watchdog_clear();
}

/*
 * The lock, MSPI transaction and unlock all run from .ram_code: a call from
 * flash text into the RAM routine goes through a linker thunk in flash, and
 * fetching it from flash after interrupts are masked stalls the bus.
 */
TLSR8258_FLASH_EXEC int tlsr8258_flash_write_page_locked(void *ctx, uint32_t addr,
							 const uint8_t *buf, size_t len)
{
	struct tlsr8258_flash_write_ctx *write_ctx = ctx;
	uint8_t key;
	int ret;

	/*
	 * No memcpy(): it lives in flash text, and fetching it while the flash
	 * is busy programming stalls the CPU. The copy stays in .ram_code.
	 */
	{
		/* volatile keeps the compiler from turning this into memcpy(). */
		volatile uint8_t *d = write_ctx->page_buf;
		const volatile uint8_t *s = buf;

		for (size_t i = 0; i < len; i++) {
			d[i] = s[i];
		}
	}
	key = tlsr8258_flash_irq_disable();
	ret = tlsr8258_flash_write_page_ram(addr, write_ctx->page_buf, len);
	tlsr8258_flash_irq_restore(key);

	return ret;
}

/* Same constraint as tlsr8258_flash_write_page_locked(). */
TLSR8258_FLASH_EXEC int tlsr8258_flash_erase_sector_locked(uint32_t addr)
{
	uint8_t key;
	int ret;

	key = tlsr8258_flash_irq_disable();
	ret = tlsr8258_flash_erase_sector_ram(addr);
	tlsr8258_flash_irq_restore(key);

	return ret;
}

/*
 * Keep the public flash API entrypoints out of regular .text: callers reach
 * them through the generic flash API from XIP code, and TC32 can wedge on the
 * first cross-region jump into a high-RAM __ramfunc target. The entrypoint
 * itself stays in the low-flash icache-locked window, while the actual MSPI
 * transaction chain remains in __ramfunc.
 */
static TLSR8258_FLASH_ENTRY int tlsr8258_flash_write(const struct device *dev,
						     off_t offset,
						     const void *data,
						     size_t len)
{
	const struct tlsr8258_flash_config *config = dev->config;
	struct tlsr8258_flash_data *dev_data = dev->data;
	const uint8_t *src = data;
	/* Page buffer kept off the caller's stack; writes hold dev_data->lock. */
	static uint8_t page_buf[TLSR8258_FLASH_PAGE_SIZE];
	struct tlsr8258_flash_write_ctx write_ctx = {
		.page_buf = page_buf,
	};
	int ret = 0;

	if (!tlsr8258_flash_range_valid(config, offset, len)) {
		return -EINVAL;
	}

	k_sem_take(&dev_data->lock, K_FOREVER);
	ret = tlsr8258_flash_call_write_pages(&write_ctx, (uint32_t)offset, src, len);

	k_sem_give(&dev_data->lock);
	return ret;
}

static TLSR8258_FLASH_ENTRY int tlsr8258_flash_erase(const struct device *dev, off_t offset,
						     size_t len)
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
		tlsr8258_watchdog_clear();
		ret = tlsr8258_flash_call_erase_sector_locked((uint32_t)offset);
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
	return tlsr8258_flash_apply_vdd_calibration(config);
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
