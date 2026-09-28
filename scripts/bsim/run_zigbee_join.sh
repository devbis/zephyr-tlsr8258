#!/bin/sh
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
zephyr_base=${ZEPHYR_BASE:-$(CDPATH= cd -- "$script_dir/../.." && pwd)}
workspace=$(CDPATH= cd -- "$zephyr_base/.." && pwd)
bsim_root=${BSIM_ROOT_PATH:-$workspace/tools/bsim}
bsim_out=${BSIM_OUT_PATH:-$bsim_root}
bsim_components=${BSIM_COMPONENTS_PATH:-$bsim_out/components}
build_root=${BSIM_ZIGBEE_BUILD_PATH:-${TMPDIR:-/tmp}/zephyr-zigbee-bsim}
sim_length=${BSIM_SIM_LENGTH:-90e6}
verbosity=${BSIM_VERBOSITY:-2}
# All nodes otherwise boot in the same simulated microsecond and the router
# and end device scan, associate and retry in lockstep, colliding on every
# attempt. Start the end device once the router has joined and sent its
# first link status, without which the coordinator cannot route to it.
ed_start_offset=${BSIM_ZIGBEE_ED_START_OFFSET:-30e6}
sim_id=${BSIM_SIM_ID:-zigbee_join_$(date +%s)}
west_cmd=${WEST:-west}
case "$(uname -s)" in
Darwin)
	toolchain=host/llvm
	;;
*)
	toolchain=host/gnu
	;;
esac
export ZEPHYR_TOOLCHAIN_VARIANT=${ZEPHYR_TOOLCHAIN_VARIANT:-$toolchain}

if ! command -v "$west_cmd" >/dev/null 2>&1 &&
	[ -x "$workspace/.venv-zephyr/bin/west" ]; then
	west_cmd=$workspace/.venv-zephyr/bin/west
fi

case "$(uname -s)" in
Darwin)
	board=${BSIM_ZIGBEE_BOARD:-nrf52_bsim/native/64}
	;;
*)
	board=${BSIM_ZIGBEE_BOARD:-nrf52_bsim}
	;;
esac

export ZEPHYR_BASE="$zephyr_base"
export BSIM_ROOT_PATH="$bsim_root"
export BSIM_OUT_PATH="$bsim_out"
export BSIM_COMPONENTS_PATH="$bsim_components"

if [ "$(uname -s)" = Darwin ]; then
	# The helper applies the required compatibility patches before building.
	"$script_dir/build_macos.sh" \
		ext_2G4_phy_v1 ext_2G4_channel_multiatt ext_2G4_modem_magic
else
	make -C "$bsim_root" \
		BSIM_COMPONENTS_PATH="$bsim_components" \
		BSIM_OUT_PATH="$bsim_out" \
		BSIM_BUILD_FAIL_ASAP=1 \
		ext_2G4_phy_v1 ext_2G4_channel_multiatt ext_2G4_modem_magic
fi

build_app() {
	name=$1
	app=$zephyr_base/samples/zigbee/native_sim_socket
	build_dir=$build_root/$name
	conf_file=$app/prj.conf
	extra_conf_file=$app/prj_bsim.conf

	# The role overlays configure the native_sim socket radio, which the
	# BabbleSim board does not use.
	if [ "$name" != ed ]; then
		extra_conf_file="$extra_conf_file;$app/$name.conf"
	fi

	if [ "$(uname -s)" = Darwin ]; then
		extra_conf_file="$extra_conf_file;$app/prj_bsim_macos.conf"
		set -- -DNATIVE_SIM_EXPERIMENTAL_MACOS=ON
	else
		set --
	fi
	"$west_cmd" build -p auto -b "$board" -d "$build_dir" "$app" -- \
		-DCONF_FILE="$conf_file" -DEXTRA_CONF_FILE="$extra_conf_file" "$@"
	binary=$bsim_out/bin/bs_nrf52_bsim_zigbee_$name
	cp "$build_dir/zephyr/zephyr.exe" "$binary"
	if [ "$(uname -s)" = Darwin ]; then
		codesign --force --sign - "$binary"
	fi
}

mkdir -p "$bsim_out/bin" "$build_root"

build_app coordinator
build_app router
build_app ed

log_dir=$build_root/$sim_id
mkdir -p "$log_dir"

coord_pid=
router_pid=
ed_pid=
phy_pid=
cleanup() {
	for pid in "$coord_pid" "$router_pid" "$ed_pid" "$phy_pid"; do
		if [ -n "$pid" ]; then
			kill "$pid" 2>/dev/null || true
		fi
	done
}
trap cleanup EXIT HUP INT TERM

cd "$bsim_out/bin"
./bs_nrf52_bsim_zigbee_coordinator -v="$verbosity" -s="$sim_id" -d=0 >"$log_dir/coordinator.log" 2>&1 &
coord_pid=$!
./bs_nrf52_bsim_zigbee_router -v="$verbosity" -s="$sim_id" -d=1 >"$log_dir/router.log" 2>&1 &
router_pid=$!
./bs_nrf52_bsim_zigbee_ed -v="$verbosity" -s="$sim_id" -d=2 \
	-start_offset="$ed_start_offset" >"$log_dir/ed.log" 2>&1 &
ed_pid=$!
# Keep the end device out of the coordinator's range so that it has to join
# through the router: device 0 is the coordinator, 2 the end device.
att_file=$log_dir/attenuation.txt
printf '0 2 : 200\n2 0 : 200\n' >"$att_file"
./bs_2G4_phy_v1 -v="$verbosity" -s="$sim_id" -D=3 -sim_length="$sim_length" \
	-channel=multiatt -argschannel -at=60 -file="$att_file" -argsmain \
	>"$log_dir/phy.log" 2>&1 &
phy_pid=$!

status=0
wait "$coord_pid" || status=1
coord_pid=
wait "$router_pid" || status=1
router_pid=
wait "$ed_pid" || status=1
ed_pid=
wait "$phy_pid" || status=1
phy_pid=
trap - EXIT HUP INT TERM

if [ "$status" -ne 0 ]; then
	cat "$log_dir"/*.log >&2
	exit "$status"
fi

for role in coordinator router ed; do
	if ! grep -q 'zigbee_shell joined network' "$log_dir/$role.log"; then
		cat "$log_dir"/*.log >&2
		echo "Zigbee $role did not report joining the network" >&2
		exit 1
	fi
done

# The coordinator interviews each device that announces itself; the router
# and the end device use IEEE addresses ...03 and ...02.
for ieee in a4c138e050020003 a4c138e050020002; do
	if ! grep -q "interview complete .*ieee=$ieee" "$log_dir/coordinator.log"; then
		cat "$log_dir"/*.log >&2
		echo "Zigbee coordinator did not complete the interview of $ieee" >&2
		exit 1
	fi
done

printf 'BabbleSim Zigbee logs: %s\n' "$log_dir"
