/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/linker/sections.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/platform/hooks.h>

extern const uintptr_t _vector_table[];
extern const uintptr_t _irq_vector_table[];

/* PHY62XX SDK 3.1.1 ROM clock entry points. */
#define ROM_CLK_INIT     ((int (*)(uint32_t))0x0000a5edU)
#define ROM_CLK_GET_PCLK ((uint32_t (*)(void))0x0000a5d1U)
#define SYS_CLK_XTAL_16M 2U
#define SYS_CLK_DLL_48M  3U
#define ROM_SPIF_CONFIG  ((int (*)(uint32_t, uint8_t, uint32_t, uint8_t, uint8_t))0x00016dc5U)
#define XFRD_FCMD_READ   0x00000003U

/* The core clock is the CPU timebase of the devicetree: the 16 MHz crystal or the 48 MHz DLL. */
#if CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC == 16000000
#define PHY6252_SYS_CLK SYS_CLK_XTAL_16M
#elif CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC == 48000000
#define PHY6252_SYS_CLK SYS_CLK_DLL_48M
#else
#error "The CPU timebase frequency must be 16 MHz or 48 MHz"
#endif

void soc_prep_hook(void)
{
	volatile uintptr_t *const jump = (volatile uintptr_t *)0x1fff0000U;
	volatile uint32_t *const reset = (volatile uint32_t *)0x40000000U;
	volatile uint32_t *const clock = (volatile uint32_t *)0x40000008U;
	volatile uint32_t *const mux_enable = (volatile uint32_t *)0x4000380cU;
	volatile uint32_t *const mux_select = (volatile uint32_t *)0x4000381cU;
	volatile uint32_t *const pull0 = (volatile uint32_t *)0x4000f008U;
	volatile uint32_t *const pull1 = (volatile uint32_t *)0x4000f00cU;

	jump[219] = _vector_table[2];
	jump[220] = _vector_table[3];
	jump[221] = _vector_table[11];
	jump[222] = _vector_table[14];
	jump[223] = _vector_table[15];
	for (uint32_t i = 0U; i < 32U; i++) {
		jump[224U + i] = _irq_vector_table[i];
	}

	/* Select the core clock and require the clock of the peripherals to match. */
	*(volatile uint8_t *)0x1fff0a4aU = PHY6252_SYS_CLK;
	if (ROM_CLK_INIT(PHY6252_SYS_CLK) != 0 ||
	    ROM_CLK_GET_PCLK() != CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC) {
		for (;;) {
			__asm__ volatile("wfi");
		}
	}

	/* UART0: P9 is TX and P10 is RX. */
	*clock |= (1U << 7) | (1U << 8);
	*reset &= ~(1U << 8);
	*reset |= 1U << 8;
	*pull0 = (*pull0 & ~(3U << 28)) | (2U << 28);
	*pull1 = (*pull1 & ~(3U << 1)) | (2U << 1);
	*mux_select = (*mux_select & ~0x003f3f00U) | 0x00050400U;
	*mux_enable |= (1U << 5) | (1U << 6);

#ifdef CONFIG_COUNTER_SNPS_DW
	/* Timers 5 and 6 share a reset and have a 4 MHz clock. */
	*clock |= (1U << 21) | (1U << 22);
	*reset &= ~(1U << 5);
	*reset |= 1U << 5;
#endif
}

#ifdef CONFIG_SERIAL
/*
 * The NS16550 driver ends its initialisation with a write of FCR that leaves the FIFO bit clear.
 * The UART then raises one interrupt per character and, with the interrupt latency of this core,
 * loses bytes from 115200 baud bursts. Enable the 16 byte FIFO again; the receive trigger stays at
 * one character.
 */
static int phy6252_uart_fifo(void)
{
	volatile uint32_t *const fcr = (volatile uint32_t *)(0x40004000U + 2U * 4U);

	*fcr = 0x07U; /* FIFO enable, clear RX and TX FIFO */

	return 0;
}

SYS_INIT(phy6252_uart_fifo, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
#endif

#ifdef CONFIG_REBOOT
/*
 * NVIC_SystemReset() resets the core only and the boot ROM does not start afterwards. The reset of
 * the vendor SDK (hal_system_soft_reset()) resets every module through SW_RESET1; the reset cause
 * the ROM reads from the retained register is set to a cold start, so that the ROM opens its UART
 * loader window as after power-up and a flash tool can connect without a power cycle.
 */
void sys_arch_reboot(int type)
{
	/* AP_AON->SLEEP_R[0] and AP_PCR->SW_RESET1 */
	volatile uint32_t *const reset_cause = (volatile uint32_t *)0x4000f0c0U;
	volatile uint32_t *const sw_reset1 = (volatile uint32_t *)0x40000004U;

	ARG_UNUSED(type);
	(void)irq_lock();
	*reset_cause = 0U; /* RSTC_COLD_UP */
	*sw_reset1 = 0U;
	for (;;) {
	}
}
#endif

/*
 * The boot ROM leaves the cache of the XIP flash bypassed: every instruction is fetched from the
 * flash by its own SPI transaction and the code runs about a hundred times slower than it can (a
 * hash of a 50 KB image took 43 s). Configure the flash read and enable the cache as the vendor SDK
 * does after the clock initialisation. The read mode of the flash changes underneath, so this runs
 * from SRAM.
 */
static __ramfunc void phy6252_xip_cache_enable(void)
{
	volatile uint32_t *const pcr_cache_gate = (volatile uint32_t *)0x40000020U;
	volatile uint32_t *const pcr_cache_rst = (volatile uint32_t *)0x40000024U;
	volatile uint32_t *const pcr_cache_bypass = (volatile uint32_t *)0x40000028U;
	volatile uint32_t *const cache_ctrl = (volatile uint32_t *)0x4000c000U;
	volatile int delay = 100;

	(void)ROM_SPIF_CONFIG(PHY6252_SYS_CLK, 1U, XFRD_FCMD_READ, 0U, 0U);

	*pcr_cache_gate |= 3U;
	*pcr_cache_rst = 2U;
	while (delay-- > 0) {
	}
	*pcr_cache_rst = 3U;

	/* Flush the tags */
	*pcr_cache_bypass = 1U;
	*cache_ctrl = 2U;
	delay = 8;
	while (delay-- > 0) {
	}
	*cache_ctrl = 3U;
	delay = 8;
	while (delay-- > 0) {
	}
	*cache_ctrl = 0U;
	*pcr_cache_bypass = 0U;
}

static int phy6252_xip_cache_init(void)
{
	phy6252_xip_cache_enable();
	return 0;
}

SYS_INIT(phy6252_xip_cache_init, PRE_KERNEL_1, 0);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(watchdog0), okay)
/*
 * The clock and the reset of the watchdog as in hal_watchdog_init() of the vendor SDK: the 32 kHz
 * RC oscillator, the clock gate, the reset of the module released and pulsed.
 */
static void phy6252_wdt_delay(void)
{
	/* The system timer does not run yet: a plain loop, as in the SDK */
	for (volatile uint32_t i = 0U; i < 40U; i++) {
	}
}

static int phy6252_wdt_hw_init(void)
{
	volatile uint32_t *const clksel = (volatile uint32_t *)0x4000f03cU;
	volatile uint32_t *const sw_reset0 = (volatile uint32_t *)0x40000000U;
	volatile uint32_t *const sw_clk1 = (volatile uint32_t *)0x40000014U;
	volatile uint32_t *const sw_reset2 = (volatile uint32_t *)0x4000000cU;

	*clksel &= ~BIT(16);
	*sw_clk1 |= BIT(5);
	if ((*sw_reset0 & BIT(2)) == 0U) {
		*sw_reset0 |= BIT(2);
		phy6252_wdt_delay();
	}
	if ((*sw_reset2 & BIT(2)) == 0U) {
		*sw_reset2 |= BIT(2);
		phy6252_wdt_delay();
	}
	*sw_reset2 &= ~BIT(5);
	phy6252_wdt_delay();
	*sw_reset2 |= BIT(5);
	phy6252_wdt_delay();
	return 0;
}

SYS_INIT(phy6252_wdt_hw_init, PRE_KERNEL_1, 1);
#endif

#ifdef CONFIG_PHY6252_WATCHDOG
/*
 * The watchdog is armed as soon as its driver is ready, so a hang in any later initialization or in
 * the application resets the chip. The hardware init above resets the module and so disarms the
 * timer that the bootloader has started.
 */
static int phy6252_wdt_arm(void)
{
	const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(watchdog0));
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_SOC,
		.window.min = 0U,
		.window.max = CONFIG_PHY6252_WATCHDOG_TIMEOUT_MS,
	};

	if (!device_is_ready(wdt) || wdt_install_timeout(wdt, &cfg) < 0 || wdt_setup(wdt, 0) != 0) {
		return -ENODEV;
	}
	return 0;
}

/* Right after the devices (priority 50), where the driver of the watchdog is initialized */
SYS_INIT(phy6252_wdt_arm, POST_KERNEL, 51);

static void phy6252_wdt_feeder(void *a, void *b, void *c)
{
	const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(watchdog0));

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		(void)wdt_feed(wdt, 0);
		k_msleep(CONFIG_PHY6252_WATCHDOG_TIMEOUT_MS / 4);
	}
}

/* The lowest priority: it runs only when nothing else wants the CPU */
K_THREAD_DEFINE(phy6252_wdt, 512, phy6252_wdt_feeder, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
#endif
