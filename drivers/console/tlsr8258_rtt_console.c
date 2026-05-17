/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/libc-hooks.h>
#include <zephyr/sys/printk-hooks.h>

#define TLSR8258_RTT_MODE_NO_BLOCK_SKIP 0u

struct tlsr8258_rtt_channel {
	const char *name;
	char *buffer;
	uint32_t size;
	volatile uint32_t write_offset;
	volatile uint32_t read_offset;
	uint32_t flags;
};

struct tlsr8258_rtt_control_block {
	char id[16];
	uint32_t max_up_channels;
	uint32_t max_down_channels;
	struct tlsr8258_rtt_channel up[1];
	struct tlsr8258_rtt_channel down[1];
};

static char tlsr8258_rtt_up_buffer[CONFIG_TLSR8258_RTT_CONSOLE_BUFFER_SIZE];
static char tlsr8258_rtt_down_buffer[1];

__aligned(16) struct tlsr8258_rtt_control_block _SEGGER_RTT;

static void tlsr8258_rtt_control_block_init(void)
{
	memset(&_SEGGER_RTT, 0, sizeof(_SEGGER_RTT));
	memcpy(_SEGGER_RTT.id, "SEGGER RTT", sizeof("SEGGER RTT"));
	_SEGGER_RTT.max_up_channels = 1u;
	_SEGGER_RTT.max_down_channels = 1u;
	_SEGGER_RTT.up[0].name = "Terminal";
	_SEGGER_RTT.up[0].buffer = tlsr8258_rtt_up_buffer;
	_SEGGER_RTT.up[0].size = sizeof(tlsr8258_rtt_up_buffer);
	_SEGGER_RTT.up[0].flags = TLSR8258_RTT_MODE_NO_BLOCK_SKIP;
	_SEGGER_RTT.down[0].name = "Terminal";
	_SEGGER_RTT.down[0].buffer = tlsr8258_rtt_down_buffer;
	_SEGGER_RTT.down[0].size = sizeof(tlsr8258_rtt_down_buffer);
	_SEGGER_RTT.down[0].flags = TLSR8258_RTT_MODE_NO_BLOCK_SKIP;
}

static size_t tlsr8258_rtt_write_no_lock(const char *data, size_t len)
{
	struct tlsr8258_rtt_channel *up = &_SEGGER_RTT.up[0];
	uint32_t write = up->write_offset;
	uint32_t read = up->read_offset;
	size_t written = 0u;

	while (written < len) {
		uint32_t next = write + 1u;

		if (next >= up->size) {
			next = 0u;
		}

		if (next == read) {
			break;
		}

		up->buffer[write] = data[written++];
		write = next;
		compiler_barrier();
		up->write_offset = write;
	}

	return written;
}

size_t tlsr8258_rtt_console_write(const char *data, size_t len)
{
	unsigned int key = irq_lock();
	size_t written;

	written = tlsr8258_rtt_write_no_lock(data, len);
	irq_unlock(key);

	return written;
}

static int tlsr8258_rtt_console_out(int character)
{
	char c = (char)character;

	(void)tlsr8258_rtt_console_write(&c, 1u);

	return character;
}

static int tlsr8258_rtt_console_init(void)
{
	tlsr8258_rtt_control_block_init();
#ifdef CONFIG_PRINTK
	__printk_hook_install(tlsr8258_rtt_console_out);
#endif
	__stdout_hook_install(tlsr8258_rtt_console_out);

	return 0;
}

SYS_INIT(tlsr8258_rtt_console_init, PRE_KERNEL_1, CONFIG_CONSOLE_INIT_PRIORITY);
