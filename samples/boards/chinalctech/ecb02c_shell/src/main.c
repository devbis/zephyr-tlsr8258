/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>

/* romdump <address> <length>: 16 bytes per line, "R <address> <hex>" */
static int cmd_romdump(const struct shell *sh, size_t argc, char **argv)
{
	static const char hex[] = "0123456789abcdef";
	const uintptr_t addr = strtoul(argv[1], NULL, 0);
	const uint32_t len = strtoul(argv[2], NULL, 0);

	for (uint32_t off = 0U; off < len; off += 16U) {
		const volatile uint8_t *p = (const volatile uint8_t *)(addr + off);
		char line[33];

		for (size_t i = 0U; i < 16U; i++) {
			line[2U * i] = hex[p[i] >> 4];
			line[2U * i + 1U] = hex[p[i] & 15U];
		}
		line[32] = '\0';
		shell_print(sh, "R %08x %s", (unsigned int)(addr + off), line);
	}
	shell_print(sh, "R done");

	return 0;
}

/* hang [lock]: a hang to try the watchdog; with "lock" the interrupts are locked as well */
static int cmd_hang(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "hanging");
	if (argc > 1) {
		(void)irq_lock();
	}
	for (;;) {
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(romdump, NULL, "romdump <address> <length>", cmd_romdump, 3, 0);
SHELL_CMD_ARG_REGISTER(hang, NULL, "hang [lock]", cmd_hang, 1, 1);

int main(void)
{
	printk("ECB02C shell: kernel reboot cold, romdump, hang\n");

	return 0;
}
