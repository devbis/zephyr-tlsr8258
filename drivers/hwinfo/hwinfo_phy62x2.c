/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/*
 * The boot ROM reads AON->SLEEP_R[0] at the start, keeps the cause in g_system_reset_cause, a variable
 * of its own RAM, and writes 1 back into the register: bit 0 is a warm start (1), bit 1 the power off
 * (2), bit 2 a warm start of the user mode (5), and no bit leaves the variable as it was. The causes 2
 * and 5 skip the window of the loader. Zephyr's reboot writes 0, so that the window opens, and a
 * software reboot is therefore reported as a cold start. The values were read from the ROM; 3 and 4,
 * the wakeup by a pad and by the RTC, are the other RSTC_* of the SDK and are not set by the ROM paths
 * read.
 */
#define G_SYSTEM_RESET_CAUSE 0x1fff0a80U

#define RSTC_COLD_UP 0U
#define RSTC_WARM_UP 1U
#define RSTC_OFF_MODE 2U
#define RSTC_WAKE_IO 3U
#define RSTC_WAKE_RTC 4U
#define RSTC_WARM_NDWC 5U

int z_impl_hwinfo_get_reset_cause(uint32_t *cause)
{
	switch (sys_read32(G_SYSTEM_RESET_CAUSE)) {
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
	sys_write32(RSTC_COLD_UP, G_SYSTEM_RESET_CAUSE);

	return 0;
}

int z_impl_hwinfo_get_supported_reset_cause(uint32_t *supported)
{
	*supported = RESET_POR | RESET_SOFTWARE | RESET_LOW_POWER_WAKE;

	return 0;
}
