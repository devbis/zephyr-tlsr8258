/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Support for the WeAct STM32WB55 core board: the USB CDC ports (console and shell on the first,
 * mcumgr or the Z-Stack host on the second) and the watchdog. Runs from SYS_INIT, so main() stays
 * the same as on the other boards.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <sample_usbd.h>
#include <zephyr/usb/usbd.h>
#endif

/*
 * The watchdog keeps counting in Standby: a deep-sleep device must not be reset by it, so its
 * timeout has to cover the longest sleep. 30 s is near the limit of the IWDG.
 */
#ifdef CONFIG_ZIGBEE_SLEEPY_DEEP
#define WDT_TIMEOUT_MS	30000U
/* A shorter period would wake the CPU too often for the deep sleep state to be chosen */
#define WDT_FEED_MS	10000
BUILD_ASSERT(CONFIG_ZIGBEE_ED_POLL_INTERVAL_MS <= 25000,
	     "the poll interval must stay below the watchdog timeout");
#else
#define WDT_TIMEOUT_MS	8000U
#define WDT_FEED_MS	1000
#endif

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

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
static int usb_start(void)
{
	struct usbd_context *usbd = sample_usbd_init_device(NULL);

	if (usbd == NULL) {
		return -ENODEV;
	}

	return usbd_enable(usbd);
}
SYS_INIT(usb_start, APPLICATION, 90);
#endif
