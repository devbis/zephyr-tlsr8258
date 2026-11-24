/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/*
 * The cause of the start is in AON->SLEEP_R[0], the RSTC_* of the PHY62XX SDK 3.1.1: 0 cold start,
 * 1 warm start, 2 power off, 3 wakeup by a pad, 4 wakeup by the RTC, 5 warm start of the user
 * mode. Zephyr's own reboot writes 0 there, so that the boot ROM opens its loader window, and
 * so a software reboot is reported as a cold start.
 */
#define AON_SLEEP_R0 0x4000f0c0U

#define RSTC_COLD_UP 0U
#define RSTC_WARM_UP 1U
#define RSTC_OFF_MODE 2U
#define RSTC_WAKE_IO 3U
#define RSTC_WAKE_RTC 4U
#define RSTC_WARM_NDWC 5U

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	switch (sys_read32(AON_SLEEP_R0)) {
	case RSTC_COLD_UP:
		*cause = RESET_POR;
		break;
	case RSTC_WARM_UP:
	case RSTC_WARM_NDWC:
		*cause = RESET_SOFTWARE;
		break;
	case RSTC_OFF_MODE:
	case RSTC_WAKE_IO:
	case RSTC_WAKE_RTC:
		*cause = RESET_LOW_POWER_WAKE;
		break;
	default:
		*cause = 0U;
		break;
	}

	return 0;
}

int z_impl_hwinfo_clear_reset_cause(void)
{
	sys_write32(RSTC_COLD_UP, AON_SLEEP_R0);

	return 0;
}

int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = RESET_POR | RESET_SOFTWARE | RESET_LOW_POWER_WAKE;

	return 0;
}
