/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Support for the WeAct STM32WB55 core board: the USB CDC ports (console and shell on the first,
 * mcumgr or the Z-Stack host on the second) and the watchdog. Runs from SYS_INIT, so main() stays
 * the same as on the other boards.
 */

#include <sample_usbd.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>

#define WDT_TIMEOUT_MS	8000U
#define WDT_FEED_MS	1000

/*
 * A hung M4 resets itself after the timeout, so MCUboot opens its update window again and the
 * image can be replaced over the serial port without touching the board.
 */
static void wdt_thread(void *a, void *b, void *c)
{
	const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	struct wdt_timeout_cfg cfg = {
		.window = { .min = 0U, .max = WDT_TIMEOUT_MS },
		.flags = WDT_FLAG_RESET_SOC,
	};
	int ch;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	ch = wdt_install_timeout(wdt, &cfg);
	if (ch < 0 || wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG) != 0) {
		return;
	}

	for (;;) {
		wdt_feed(wdt, ch);
		k_msleep(WDT_FEED_MS);
	}
}

K_THREAD_DEFINE(wdt_tid, 512, wdt_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

static int usb_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(NULL);

	if (usbd == NULL) {
		return -ENODEV;
	}

	return usbd_enable(usbd);
}
SYS_INIT(usb_start, APPLICATION, 90);
