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
fallback_c="subsys/zigbee/mac/mac_trx_compat.c"

model_id="$(sed -n 's/^#define[[:space:]][[:space:]]*APP_PROFILE_MODEL_ID[[:space:]][[:space:]]*"\(.*\)"/\1/p' "$profile_h")"
fallback_model_id="$(
	sed -n '/zb_platform_app_basic_model_id/,/}/s/.*return "\(.*\)".*/\1/p' "$fallback_c" |
	head -n 1
)"

test -n "$model_id"
test -n "$fallback_model_id"
test "$fallback_model_id" = "$model_id"
