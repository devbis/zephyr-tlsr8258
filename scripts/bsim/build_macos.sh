#!/bin/sh
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
zephyr_base=${ZEPHYR_BASE:-$(CDPATH= cd -- "$script_dir/../.." && pwd)}
workspace=$(CDPATH= cd -- "$zephyr_base/.." && pwd)
bsim_root=${BSIM_ROOT_PATH:-$workspace/tools/bsim}
bsim_components=${BSIM_COMPONENTS_PATH:-$bsim_root/components}
nrf_hw_models=${ZEPHYR_NRF_HW_MODELS_MODULE:-$workspace/modules/bsim_hw_models/nrf_hw_models}
bsim_out=${BSIM_OUT_PATH:-$bsim_root}
python3=${PYTHON3:-python3}

"$python3" "$script_dir/apply_macos_patches.py" \
    --bsim-components "$bsim_components" \
    --nrf-hw-models "$nrf_hw_models"

if [ "$#" -eq 0 ]; then
    set -- ext_2G4_channel_NtNcable ext_2G4_modem_magic
fi

exec make -C "$bsim_root" \
    BSIM_COMPONENTS_PATH="$bsim_components" \
    BSIM_OUT_PATH="$bsim_out" \
    BSIM_BUILD_FAIL_ASAP=1 \
    "$@"
