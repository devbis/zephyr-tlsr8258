#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-sim"
cc="${CC:-cc}"

if rg -n '#include <zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge\.h>' \
	subsys/zigbee \
	-g '*.c' -g '*.h' \
	-g '!subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c'; then
	echo "Generic Zigbee subsystem files must use zb_radio_port.h, not tlsr8258_zigbee_bridge.h" >&2
	exit 1
fi

if rg -n 'rf_set(Channel|TrxState)\s*\(' \
	subsys/zigbee \
	-g '*.c' \
	-g '!subsys/zigbee/mac/mac_phy.c' \
	-g '!subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c'; then
	echo "Generic Zigbee runtime must not call rf_setChannel/rf_setTrxState directly" >&2
	exit 1
fi

if rg -n 'DEVICE_DT_GET\(DT_NODELABEL\(zb\)\)' \
	subsys/zigbee \
	-g '*.c' \
	-g '!subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c'; then
	echo "DEVICE_DT_GET(DT_NODELABEL(zb)) must stay inside the TLSR8258 radio port adapter" >&2
	exit 1
fi

if rg -n 'S_TIMER_CLOCK_1US[[:space:]]+24|sysTimerPerUs[[:space:]]*=[[:space:]]*24' \
	subsys/zigbee/platform/zephyr subsys/zigbee/mac; then
	echo "Zigbee timer conversion must use Zephyr clock conversion, not a fixed 24 MHz assumption" >&2
	exit 1
fi

if rg -n 'CONFIG_ZIGBEE_MAC_TIMER_CYCLES_PER_US' \
	subsys/zigbee/platform/zephyr subsys/zigbee/mac \
	-g '*.c' -g '*.h'; then
	echo "Generic Zigbee timing path must not depend on CONFIG_ZIGBEE_MAC_TIMER_CYCLES_PER_US" >&2
	exit 1
fi

if ! rg -q 'tl_zbRxTaskPost\(mac_rxDataParse, buf\)' subsys/zigbee/mac/mac_trx.c; then
	echo "MAC RX frames must use the dedicated deferred RX task lane" >&2
	exit 1
fi

if rg -n 'K_MSGQ_DEFINE\(g_radio_rx_msgq|ZB_RADIO_RX_WORK_Q_DEPTH[[:space:]]+4U|memcpy\(rx_target, item->dma' \
	subsys/zigbee/platform/zephyr/drv_radio_zephyr.c; then
	echo "Radio RX deferred path must use a single-copy slot pool, not msgq depth 4 plus ring copy" >&2
	exit 1
fi

if rg -n 'struct tlsr8258_rx_frame_view[[:space:]]*\{' \
	subsys/zigbee/platform/zephyr/drv_radio_zephyr.c; then
	echo "drv_radio_zephyr.c must use the shared tlsr8258_rx_frame_view from zb_radio_port.h" >&2
	exit 1
fi

if rg -n 'struct zb_radio_rx_slot|g_radio_rx_work|rx_pending_(head|tail|count)|zb_radio_rx_slot_(alloc|pop|release)|zb_radio_rx_work_handler' \
	subsys/zigbee/platform/zephyr/drv_radio_zephyr.c; then
	echo "drv_radio_zephyr.c must stay a thin Zigbee RX sink without bridge-owned RX queue/work" >&2
	exit 1
fi

if rg -n 'sector_size[[:space:]]*=[[:space:]]*4096|CONFIG_TC32' \
	subsys/zigbee/platform/zephyr/drv_nv_zephyr.c; then
	echo "Zigbee NV backend must derive flash geometry and avoid CONFIG_TC32 skip paths" >&2
	exit 1
fi

if rg -n 'SYS_INIT\s*\(\s*zb_nvs_init\s*,' \
	subsys/zigbee/platform/zephyr/drv_nv_zephyr.c; then
	echo "Zigbee NV backend must not mount NVS from SYS_INIT; initialize it lazily from runtime context" >&2
	exit 1
fi

leave_start_line="$(rg -n 'tl_zbNwkNlmeLeaveRequestHandler\(arg\)' \
	subsys/zigbee/zdo/zdp_services.c | head -n 1 | cut -d: -f1 || true)"
leave_clear_line="$(rg -n 'zb_platform_clear_persistent_state' \
	subsys/zigbee/zdo/zdp_services.c | head -n 1 | cut -d: -f1 || true)"
if [ -n "$leave_start_line" ] && [ -n "$leave_clear_line" ] &&
	[ "$leave_start_line" -gt "$leave_clear_line" ]; then
	echo "local leave/reset must follow the vendor leave transaction" >&2
	exit 1
fi

"$cc" -std=c17 -Wall -Wextra -Werror \
	tests/subsys/zigbee/host_sim/main.c \
	-o "$out"

"$out"
