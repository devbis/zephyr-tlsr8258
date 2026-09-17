#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Generate an ld64 order file for native simulator HW events."""

import argparse
import re
from pathlib import Path

EVENT_RE = re.compile(
    r"(?<!#define\s)NSI_HW_EVENT\s*\(\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"([0-9]+)\s*\)"
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


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("sources", nargs="+", type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    symbols = collect_events(args.sources)
    args.output.write_text("\n".join(symbols) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
