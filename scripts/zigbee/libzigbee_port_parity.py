#!/usr/bin/env python3
"""Audit the recovered libzigbee source set and optional role symbols.

The source list is deliberately read from the vendor CMake file instead of
being duplicated in this script.  The manifest remains human-readable, while
this check catches a newly added vendor source that was forgotten in the port
audit.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


SOURCE_SET_RE = re.compile(
    r"set\(ZB_RECOVERED_SOURCES(?P<body>.*?)\n\)", re.DOTALL
)
SOURCE_RE = re.compile(r"src/([A-Za-z0-9_./-]+\.c)")
MANIFEST_ROW_RE = re.compile(r"^\|([^|]+)\|([^|]+)\|([^|]+)\|$")
BACKTICK_RE = re.compile(r"`([^`]+)`")

SPECIAL_IMPLEMENTATIONS = {
    "zb_af_data.c": ("zdo/zdo_zephyr_glue.c",),
    "zb_buffer.c": ("platform/zephyr/zb_buffer_zephyr.c",),
    "zb_task_queue.c": (
        "platform/zephyr/zb_task_queue_zephyr.c",
        "platform/zephyr/zb_task_queue_router.c",
    ),
    "second_clock.c": ("platform/zephyr/zb_second_clock.c",),
    "bdb_base.c": (
        "bdb/bdb_base.c",
        "bdb/bdb.c",
        "platform/zephyr/zb_bdb_bootstrap.c",
    ),
    "nwk_test.c": (),
}


def fail(message: str) -> int:
    print(f"parity: error: {message}", file=sys.stderr)
    return 1


def vendor_sources(cmake_path: Path) -> list[str]:
    text = cmake_path.read_text(encoding="utf-8")
    match = SOURCE_SET_RE.search(text)
    if match is None:
        raise ValueError(f"ZB_RECOVERED_SOURCES not found in {cmake_path}")
    return SOURCE_RE.findall(match.group("body"))


def manifest_sources(manifest_path: Path) -> dict[str, tuple[str, str]]:
    result: dict[str, tuple[str, str]] = {}
    for line in manifest_path.read_text(encoding="utf-8").splitlines():
        match = MANIFEST_ROW_RE.match(line)
        if match is None:
            continue
        source_cell, implementation, classification = match.groups()
        for group in BACKTICK_RE.findall(source_cell):
            for source in (part.strip() for part in group.split(",")):
                if source.endswith(".c"):
                    result[source] = (implementation.strip(), classification.strip())
    return result


def implementation_exists(repo_root: Path, source: str) -> bool:
    candidates = SPECIAL_IMPLEMENTATIONS.get(source)
    if candidates is not None:
        if source == "nwk_test.c":
            return True
        return all((repo_root / "subsys/zigbee" / path).is_file() for path in candidates)

    return any(path.name == source for path in (repo_root / "subsys/zigbee").rglob("*.c"))


def exported_symbols(binary: Path) -> set[str]:
    # Zephyr's linker may keep a recovered C function local to the final
    # image after section garbage collection.  It is still a valid parity
    # symbol, so inspect all defined symbols rather than only global ones.
    commands = (("nm", "-U", str(binary)), ("nm", str(binary)))
    symbols: set[str] = set()
    for command in commands:
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        if completed.returncode == 0:
            for line in completed.stdout.splitlines():
                fields = line.split()
                if fields:
                    symbols.add(fields[-1].removeprefix("_"))

    # The native_sim runner is a second-stage Mach-O link and the TC32 ELF
    # uses an architecture unknown to the host nm.  Both still emit a linker
    # map with the final kept symbol names, so use the sibling map as a
    # portable fallback (and merge it when nm returned only partial data).
    map_path = binary.with_name("zephyr.map")
    if map_path.is_file():
        map_text = map_path.read_text(encoding="utf-8")
        # Mach-O maps print an underscore before C symbols.  The TC32 linker
        # map prints the same symbols without it, after four address/size
        # columns (for example: ``a1d1 a1d1 10 1 zb_assocJoinReq``).
        for token in re.findall(
            r"(?<![A-Za-z0-9_])_[A-Za-z][A-Za-z0-9_]*", map_text
        ):
            symbols.add(token.removeprefix("_"))
        for match in re.finditer(
            r"^\s*[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+"
            r"[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+"
            r"([A-Za-z_][A-Za-z0-9_]*)\s*$",
            map_text,
            re.MULTILINE,
        ):
            symbols.add(match.group(1).removeprefix("_"))

    if not symbols:
        raise ValueError(f"cannot inspect symbols in {binary}")
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor-cmake", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--required-symbol", action="append", default=[])
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    vendor_cmake = args.vendor_cmake or repo_root.parent / "libzigbee/CMakeLists.txt"
    manifest = args.manifest or repo_root / "docs/libzigbee-port-manifest.md"

    try:
        sources = vendor_sources(vendor_cmake)
        audited = manifest_sources(manifest)
    except (OSError, ValueError) as error:
        return fail(str(error))

    errors: list[str] = []
    if len(sources) != len(set(sources)):
        errors.append("vendor source list contains duplicate entries")

    for source in sources:
        if source not in audited:
            errors.append(f"{source}: missing manifest entry")
            continue
        implementation, classification = audited[source]
        if classification.lower() == "pending":
            errors.append(f"{source}: manifest classification is still pending")
        if not implementation_exists(repo_root, source):
            errors.append(f"{source}: implementation target is absent ({implementation})")

    extras = sorted(set(audited) - set(sources))
    if extras:
        errors.append("manifest contains sources not in vendor list: " + ", ".join(extras))

    if args.binary:
        try:
            symbols = exported_symbols(args.binary)
        except (OSError, ValueError) as error:
            return fail(str(error))
        missing = sorted(set(args.required_symbol) - symbols)
        if missing:
            errors.append(
                f"{args.binary}: missing required symbols: " + ", ".join(missing)
            )

    if errors:
        for error in errors:
            print(f"parity: error: {error}", file=sys.stderr)
        return 1

    print(f"parity: {len(sources)} vendor sources audited")
    if args.binary:
        print(f"parity: symbols checked in {args.binary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
