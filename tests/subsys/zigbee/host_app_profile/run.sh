#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-app-profile"
cc="${CC:-cc}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	tests/subsys/zigbee/host_app_profile/main.c \
	-o "$out"

"$out"

profile_h="samples/zigbee/zigbee_shell/src/app_profile.h"
profile_c="samples/zigbee/zigbee_shell/src/app_profile.c"

model_id="$(sed -n 's/^#define[[:space:]][[:space:]]*APP_PROFILE_MODEL_ID[[:space:]][[:space:]]*"\(.*\)"/\1/p' "$profile_h")"
test -n "$model_id"
rg -q 'return APP_PROFILE_MODEL_ID;' "$profile_c"
