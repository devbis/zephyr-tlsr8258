#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Generate an ld64 order file for native simulator ordered entries."""

import argparse
import re
import subprocess
from pathlib import Path

EVENT_RE = re.compile(
    r"(?<!#define\s)NSI_HW_EVENT\s*\(\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"([0-9]+)\s*\)"
)

NSI_TASK_RE = re.compile(
    r"(?<!#define\s)NSI_TASK\s*\(\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"([0-9]+)\s*\)"
)

NSI_TASK_LEVELS = {
    "PRE_BOOT_1": 0,
    "PRE_BOOT_2": 1,
    "HW_INIT": 2,
    "PRE_BOOT_3": 3,
    "FIRST_SLEEP": 4,
    "ON_EXIT_PRE": 5,
    "ON_EXIT_POST": 6,
}

NATIVE_TASK_LEVELS = (
    "PRE_BOOT_1",
    "PRE_BOOT_2",
    "PRE_BOOT_3",
    "FIRST_SLEEP",
    "ON_EXIT",
)

NATIVE_TASK_SYMBOL_RE = re.compile(
    r"(?P<prefix>_+)native_task_(?P<level>PRE_BOOT_[123]|FIRST_SLEEP|ON_EXIT)_"
    r"(?P<priority>[0-9]+)_(?P<function>[A-Za-z_][A-Za-z0-9_]*)$"
)

MACHO_INIT_LEVELS = (
    "EARLY",
    "PRE_KERNEL_1",
    "PRE_KERNEL_2",
    "POST_KERNEL",
    "APPLICATION",
    "SMP",
)

MACHO_INIT_SYMBOL_RE = re.compile(
    r"___init_order_(?P<level>EARLY|PRE_KERNEL_[12]|POST_KERNEL|APPLICATION|SMP)_"
    r"(?P<priority>[0-9]+)_"
    r"(?P<sub_priority>[0-9]+)_(?P<name>[A-Za-z_][A-Za-z0-9_]*)$"
)

MACHO_INIT_ARRAY_SYMBOL_RE = re.compile(
    r"___zephyr_init_array_(?P<bound>start|end)_(?P<image>[0-9]+)$"
)

def collect_events(paths):
    """Return HW event symbols in priority order."""
    events = []
    seen = set()

    for path in paths:
        text = Path(path).read_text(encoding="utf-8")
        for timer, function, priority in EVENT_RE.findall(text):
            symbol = f"___nsi_hw_event_{function}{timer}"
            if symbol in seen:
                continue
            seen.add(symbol)
            events.append((int(priority), symbol))

    return [symbol for _, symbol in sorted(events, key=lambda event: event[0])]


def collect_symbols(paths):
    """Return native simulator symbols in linker order."""
    tasks = {level: [] for level in NSI_TASK_LEVELS}
    seen = set()

    for path in paths:
        text = Path(path).read_text(encoding="utf-8")
        for function, level, priority in NSI_TASK_RE.findall(text):
            if level not in NSI_TASK_LEVELS:
                continue
            symbol = f"___nsi_task_{function}"
            if symbol in seen:
                continue
            seen.add(symbol)
            tasks[level].append((int(priority), symbol))

    symbols = collect_events(paths)
    for level in NSI_TASK_LEVELS:
        symbols.append(f"___nsi_task_range_start_{level}")
        symbols.extend(
            symbol for _, symbol in sorted(tasks[level], key=lambda task: task[0])
        )
        symbols.append(f"___nsi_task_range_end_{level}")

    return symbols


def collect_native_task_symbols(nm, objects):
    """Return compiled native task symbols in priority order."""
    tasks = {level: [] for level in NATIVE_TASK_LEVELS}
    seen = set()

    for obj in objects:
        result = subprocess.run(
            [str(nm), "-j", str(obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        for line in result.stdout.splitlines():
            match = NATIVE_TASK_SYMBOL_RE.fullmatch(line.strip())
            if match is None or match.group("level") not in NATIVE_TASK_LEVELS:
                continue

            symbol = line.strip()
            if symbol in seen:
                continue
            seen.add(symbol)
            tasks[match.group("level")].append((int(match.group("priority")), symbol))

    symbols = []
    for level in NATIVE_TASK_LEVELS:
        symbols.append(f"___native_task_range_start_{level}")
        symbols.extend(
            symbol
            for _, symbol in sorted(tasks[level], key=lambda task: task[0])
        )
        symbols.append(f"___native_task_range_end_{level}")

    return symbols


def collect_init_symbols(nm, objects):
    """Return compiled Mach-O init-entry symbols in priority order."""
    entries = {level: [] for level in MACHO_INIT_LEVELS}
    init_array_bounds = {}
    seen = set()

    for obj in objects:
        result = subprocess.run(
            [str(nm), "-j", str(obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        for line in result.stdout.splitlines():
            symbol = line.strip()
            init_array_match = MACHO_INIT_ARRAY_SYMBOL_RE.fullmatch(symbol)
            if init_array_match is not None:
                image = int(init_array_match.group("image"))
                bounds = init_array_bounds.setdefault(image, {})
                bounds[init_array_match.group("bound")] = symbol
                continue

            match = MACHO_INIT_SYMBOL_RE.fullmatch(symbol)
            if match is None or symbol in seen:
                continue

            seen.add(symbol)
            entries[match.group("level")].append(
                (int(match.group("priority")), int(match.group("sub_priority")), symbol)
            )

    symbols = []
    for name in MACHO_INIT_LEVELS:
        symbols.append(f"___macho_init_range_start_{name}")
        symbols.extend(
            symbol
            for _, _, symbol in sorted(entries[name], key=lambda entry: (entry[0], entry[1]))
        )
        symbols.append(f"___macho_init_range_end_{name}")

    for image in sorted(init_array_bounds):
        bounds = init_array_bounds[image]
        if {"start", "end"}.issubset(bounds):
            symbols.extend((bounds["start"], bounds["end"]))

    return symbols


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--nm", type=Path)
    parser.add_argument("--object", action="append", default=[], type=Path)
    parser.add_argument("sources", nargs="*", type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    symbols = collect_symbols(args.sources)
    if args.nm is not None:
        symbols.extend(collect_native_task_symbols(args.nm, args.object))
        symbols.extend(collect_init_symbols(args.nm, args.object))
    args.output.write_text("\n".join(symbols) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
