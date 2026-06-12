#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="tests/subsys/zigbee/host_shell_router_bootstrap/host_router_bootstrap_test"
cc="${CC:-cc}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	-Itests/subsys/zigbee/host_shell_bootstrap/include \
	tests/subsys/zigbee/host_shell_router_bootstrap/main.c \
	-o "$out"

"$out"
