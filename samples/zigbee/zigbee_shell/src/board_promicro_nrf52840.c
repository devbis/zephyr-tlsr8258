/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hal/nrf_power.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#if defined(CONFIG_ZIGBEE_ED)
#include "zb_common.h"
#endif

static void enter_uf2(void)
{
	NRF_POWER->GPREGRET = 0x57U;
	sys_reboot(SYS_REBOOT_COLD);
}

static int cmd_uf2(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	enter_uf2();
	return 0;
}

SHELL_CMD_REGISTER(uf2, NULL, "Reboot into the UF2 bootloader", cmd_uf2);

#if defined(CONFIG_ZIGBEE_ED)
static int cmd_zb_diag(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "MAC RX unicast: %u, TX unicast: %u", g_sysDiags.macRxUcast,
		    g_sysDiags.macTxUcast);
	shell_print(shell, "NWK decrypt failures: %u, frame counter failures: %u",
		    g_sysDiags.nwkDecryptFailures, g_sysDiags.nwkFCFailure);
	shell_print(shell, "APS decrypt failures: %u, frame counter failures: %u",
		    g_sysDiags.apsDecryptFailures, g_sysDiags.apsFCFailure);
	shell_print(shell, "Buffer allocation failures: %u", g_sysDiags.packetBufferAllocateFailures);
	return 0;
}

SHELL_CMD_REGISTER(zb_diag, NULL, "Show Zigbee diagnostics", cmd_zb_diag);
#endif
