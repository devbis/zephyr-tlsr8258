#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Generate Mach-O metadata for Zephyr iterable sections."""

import argparse
import hashlib
import re
from pathlib import Path

from iter_sections import parse_tagged_items


MAX_SECTION_NAME_LENGTH = 16


def shorten(name):
    """Return a deterministic Mach-O section name for a logical section."""
    return f"z{hashlib.sha256(name.encode()).hexdigest()[:15]}"


def build_mapping(names, shorten=shorten):
    """Build a deterministic logical-to-Mach-O section name mapping."""
    names = list(names)
    if len(names) != len(set(names)):
        raise ValueError("duplicate logical section name")

    mapping = {}
    reverse = {}
    for name in sorted(names):
        short_name = shorten(name)
        if len(short_name) > MAX_SECTION_NAME_LENGTH:
            raise ValueError(f"Mach-O section name is too long: {short_name}")
        if not short_name.isascii():
            raise ValueError(f"Mach-O section name is not ASCII: {short_name}")
        previous_name = reverse.get(short_name)
        if previous_name is not None and previous_name != name:
            raise ValueError(
                f"short section name collision: {previous_name} and {name} -> {short_name}"
            )
        mapping[name] = short_name
        reverse[short_name] = name

    return mapping


def write_outputs(mapping, header_path, aliases_path, linker_path, alias_names=None):
    """Write generated compiler, alias, and linker metadata."""
    header_path = Path(header_path)
    aliases_path = Path(aliases_path)
    linker_path = Path(linker_path)

    header_lines = [
        "/* Generated file. Do not edit. */",
        "#ifndef ZEPHYR_GENERATED_MACHO_ITER_SECTIONS_H_",
        "#define ZEPHYR_GENERATED_MACHO_ITER_SECTIONS_H_",
        "",
    ]
    for name, short_name in mapping.items():
        header_lines.append(f"#define Z_MACHO_SEC_{'_' + name} {short_name}")
        header_lines.append(f"#ifndef Z_MACHO_SEC_{name}")
        header_lines.append(f"#define Z_MACHO_SEC_{name} {short_name}")
        header_lines.append("#endif")
    header_lines.extend(["", "#endif /* ZEPHYR_GENERATED_MACHO_ITER_SECTIONS_H_ */", ""])
    header_path.write_text("\n".join(header_lines), encoding="utf-8")

    if alias_names is None:
        alias_names = mapping
    missing_aliases = sorted(set(alias_names) - mapping.keys())
    if missing_aliases:
        raise ValueError(f"alias sections are not in mapping: {missing_aliases}")

    alias_lines = [
        "/* Generated file. Do not edit. */",
        "#ifdef __APPLE__",
        "#define MACHO_ALIAS(sym, target) \\",
        '\t__asm__(".globl _" #sym "\\n_" #sym " = " target)',
        "",
    ]
    for name in alias_names:
        short_name = mapping[name]
        alias_lines.append(
            f'MACHO_ALIAS(_{name}_list_start, "section$start$__DATA${short_name}");'
        )
        alias_lines.append(
            f'MACHO_ALIAS(_{name}_list_end, "section$end$__DATA${short_name}");'
        )
    alias_lines.extend(["", "#endif /* __APPLE__ */", ""])
    aliases_path.write_text("\n".join(alias_lines), encoding="utf-8")

    linker_lines = ["/* Generated file. Do not edit. */", ""]
    for name, short_name in mapping.items():
        linker_lines.append(f"/* {name} -> {short_name} */")
        linker_lines.append(f"KEEP(*(SORT_BY_NAME({short_name})))")
    linker_lines.append("")
    linker_path.write_text("\n".join(linker_lines), encoding="utf-8")


def parse_source_names(paths):
    """Extract section names used by iterable-section compiler/linker helpers."""
    names = []
    patterns = [
        (re.compile(r"zephyr_iterable_section\s*\(\s*NAME\s+([A-Za-z0-9_]+)"), str),
        (re.compile(r"Z_LINK_ITERABLE\s*\(\s*([A-Za-z0-9_]+)"), str),
        (re.compile(r"STRUCT_SECTION_ITERABLE(?:_ALTERNATE)?\s*\(\s*([A-Za-z0-9_]+)"), str),
        (
            re.compile(
                r"TYPE_SECTION_ITERABLE(?:_ARRAY)?\s*\([^,()\n]+,[^,()\n]+,\s*([A-Za-z0-9_]+)"
            ),
            str,
        ),
        (
            re.compile(
                r"__in_section(?:_unique)?\s*\(\s*(_*[A-Za-z0-9][A-Za-z0-9_]*)"
            ),
            lambda name: name[1:] if name.startswith("_") else name,
        ),
        (re.compile(r"_NOINIT_SECTION_NAME\s+([A-Za-z0-9_]+)"), str),
    ]
    for path in paths:
        text = Path(path).read_text(encoding="utf-8")
        for pattern, transform in patterns:
            names.extend(transform(name) for name in pattern.findall(text))
    return names


def parse_linker_script_names(paths):
    """Extract iterable section names from linker scripts with list symbols."""
    names = []
    pattern = re.compile(r"_([A-Za-z0-9_]+)_list_start\s*=")
    for path in paths:
        names.extend(pattern.findall(Path(path).read_text(encoding="utf-8")))
    return names


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--struct-tags", type=Path)
    parser.add_argument("--tag", default="__subsystem")
    parser.add_argument("--source", action="append", type=Path, default=[])
    parser.add_argument("--source-dir", action="append", type=Path, default=[])
    parser.add_argument("--linker-script", action="append", type=Path, default=[])
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--aliases", required=True, type=Path)
    parser.add_argument("--alias-input", type=Path)
    parser.add_argument("--linker", required=True, type=Path)
    return parser.parse_args()


def main():
    args = parse_args()
    names = []
    if args.input is not None:
        names.extend(
            line.strip()
            for line in args.input.read_text(encoding="utf-8").splitlines()
            if line.strip()
        )
    if args.struct_tags is not None:
        names.extend(parse_tagged_items(args.struct_tags, args.tag)[0])
    names.extend(parse_source_names(args.source))
    source_files = [
        path
        for source_dir in args.source_dir
        for path in source_dir.rglob("*")
        if path.suffix in {".c", ".h", ".cmake", ".ld"}
    ]
    names.extend(parse_source_names(source_files))
    names.extend(
        parse_linker_script_names(path for path in source_files if path.suffix == ".ld")
    )
    names.extend(parse_linker_script_names(args.linker_script))
    if not names:
        raise ValueError("no iterable section names were provided")
    mapping = build_mapping(sorted(set(names)))
    alias_names = None
    if args.alias_input is not None:
        alias_names = [
            line.strip()
            for line in args.alias_input.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
    write_outputs(mapping, args.header, args.aliases, args.linker, alias_names)


if __name__ == "__main__":
    main()
