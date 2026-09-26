#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Apply the local macOS patches to the BabbleSim dependencies."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


def _run_git(repo: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repo), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )


def _apply_patch(repo: Path, patch: Path) -> None:
    check = _run_git(repo, "apply", "--unidiff-zero", "--check", str(patch))
    if check.returncode == 0:
        result = _run_git(repo, "apply", "--unidiff-zero", str(patch))
        if result.returncode != 0:
            raise RuntimeError(
                f"failed to apply {patch.name} in {repo}:\n"
                f"{result.stdout}{result.stderr}"
            )
        print(f"applied {patch.name} to {repo}")
        return

    reverse_check = _run_git(
        repo, "apply", "--unidiff-zero", "--reverse", "--check", str(patch)
    )
    if reverse_check.returncode == 0:
        print(f"already applied {patch.name} to {repo}")
        return

    raise RuntimeError(
        f"{patch.name} does not apply to {repo}:\n"
        f"{check.stdout}{check.stderr}"
    )


def _default_paths() -> tuple[Path, Path]:
    zephyr_base = Path(
        os.environ.get("ZEPHYR_BASE", Path(__file__).resolve().parents[2])
    ).resolve()
    workspace = zephyr_base.parent
    bsim_components = Path(
        os.environ.get("BSIM_COMPONENTS_PATH", workspace / "tools/bsim/components")
    ).resolve()
    nrf_hw_models = Path(
        os.environ.get(
            "ZEPHYR_NRF_HW_MODELS_MODULE",
            workspace / "modules/bsim_hw_models/nrf_hw_models",
        )
    ).resolve()
    nordic_hal = Path(
        os.environ.get("ZEPHYR_HAL_NORDIC_MODULE", workspace / "modules/hal/nordic")
    ).resolve()
    return bsim_components, nrf_hw_models, nordic_hal


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    defaults = _default_paths()
    parser.add_argument("--bsim-components", type=Path, default=defaults[0])
    parser.add_argument("--nrf-hw-models", type=Path, default=defaults[1])
    parser.add_argument("--nordic-hal", type=Path, default=defaults[2])
    args = parser.parse_args()

    if sys.platform != "darwin":
        parser.error("the macOS dependency patches are only needed on macOS")

    patch_dir = Path(__file__).resolve().parent / "patches"
    repositories = (
        (args.bsim_components.resolve(), patch_dir / "babblesim-macos.patch"),
        (args.nrf_hw_models.resolve(), patch_dir / "nrf_hw_models-macos.patch"),
        (args.nrf_hw_models.resolve(), patch_dir / "nrf_hw_models-bsim-64bit.patch"),
        (args.nrf_hw_models.resolve(), patch_dir / "nrf_hw_models-bsim-64bit-endpoints.patch"),
        (args.nrf_hw_models.resolve(), patch_dir / "nrf_hw_models-bsim-64bit-dma.patch"),
        (args.nrf_hw_models.resolve(), patch_dir / "nrf_hw_models-bsim-radio-abort-timing.patch"),
        (args.nordic_hal.resolve(), patch_dir / "hal_nordic-bsim-64bit.patch"),
        (args.nordic_hal.resolve(), patch_dir / "hal_nordic-bsim-ack-timing.patch"),
    )

    for repository, patch in repositories:
        if not repository.is_dir():
            parser.error(f"repository does not exist: {repository}")
        if not patch.is_file():
            parser.error(f"patch does not exist: {patch}")
        try:
            _apply_patch(repository, patch)
        except RuntimeError as error:
            parser.error(str(error))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
