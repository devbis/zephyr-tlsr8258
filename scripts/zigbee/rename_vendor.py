#!/usr/bin/env python3
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Give the imported Zigbee sources Zephyr names.

The vendor sources use their own integer types (u8, u32, ...) and a tl_/TL_
prefix on their identifiers. This script rewrites both across the Zigbee
subsystem, its samples and its tests, so that nothing the application sees
carries a vendor name:

  u8 u16 u32 u64 u24   ->  uint8_t uint16_t uint32_t uint64_t uint32_t
  s8 s16 s32 s64 s24   ->  int8_t  int16_t  int32_t  int64_t  int32_t
  tl_zbFoo / TL_ZB_FOO ->  zb_Foo / ZB_FOO
  tl_foo / TL_FOO      ->  zb_foo / ZB_FOO

Not renamed: include file names (tl_common.h, tl_platform.h, tl_zb_mac.h,
tl_zb_mac_pib.h) and the upstream name tl_zigbee_sdk.

It runs after scripts/zigbee/import_vendor.sh and the adaptation commits: the
import commits stay verbatim, and the rename is the last commit on top. The
script is idempotent and refuses to run if a new name already exists.

Usage: rename_vendor.py [--dry-run]
"""

import os
import re
import subprocess
import sys

ROOTS = ("subsys/zigbee", "samples/zigbee", "tests/subsys/zigbee")
SUFFIXES = (".c", ".h")

TYPES = {
    "u8": "uint8_t", "u16": "uint16_t", "u32": "uint32_t", "u64": "uint64_t", "u24": "uint32_t",
    "s8": "int8_t", "s16": "int16_t", "s32": "int32_t", "s64": "int64_t", "s24": "int32_t",
}

KEEP = {"tl_zigbee_sdk", "tl_common", "tl_platform", "tl_zb_mac", "tl_zb_mac_pib"}

TL_RE = re.compile(r"\b(?:tl_[A-Za-z0-9_]+|TL_[A-Za-z0-9_]+)\b")
TYPE_RE = re.compile(r"\b(?:" + "|".join(TYPES) + r")\b")
TOKEN_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")

# The vendor typedefs this script makes unnecessary.
TYPEDEF_BLOCK = re.compile(
    r"typedef uint8_t\s+u8;\ntypedef int8_t\s+s8;\n"
    r"typedef uint16_t\s+u16;\ntypedef int16_t\s+s16;\n"
    r"typedef uint32_t\s+u32;\ntypedef int32_t\s+s32;\n"
    r"typedef uint64_t\s+u64;\ntypedef int64_t\s+s64;\n\n"
    r"typedef u32 u24;\ntypedef s32 s24;\n")


def new_name(name):
    if name.startswith("tl_zb"):
        return "zb_" + name[5:].lstrip("_") if name[5:6] != "_" else "zb_" + name[6:]
    if name.startswith("TL_ZB_"):
        return "ZB_" + name[6:]
    if name.startswith("tl_"):
        return "zb_" + name[3:]
    return "ZB_" + name[3:]


def source_files():
    for root in ROOTS:
        for d, _, files in os.walk(root):
            for f in files:
                if f.endswith(SUFFIXES):
                    yield os.path.join(d, f)


def main():
    dry = "--dry-run" in sys.argv[1:]
    top = subprocess.check_output(["git", "rev-parse", "--show-toplevel"], text=True).strip()
    os.chdir(top)

    files = list(source_files())
    texts = {}
    tokens = set()
    for f in files:
        with open(f, encoding="utf-8", errors="surrogateescape") as fh:
            texts[f] = fh.read()
        tokens.update(TOKEN_RE.findall(texts[f]))

    def tl_sub(m):
        name = m.group()
        if name in KEEP or name.startswith("tl_zigbee"):
            return name
        # Keep a file name such as tl_zb_mac.h as it is.
        if m.string[m.end():m.end() + 2] == ".h":
            return name
        return new_name(name)

    mapping = {}
    for text in texts.values():
        for m in TL_RE.finditer(text):
            if tl_sub(m) != m.group():
                mapping[m.group()] = tl_sub(m)

    clashes = sorted(old for old, new in mapping.items() if new in tokens and new != old)
    inverse = {}
    for old, new in mapping.items():
        inverse.setdefault(new, []).append(old)
    many = {new: olds for new, olds in inverse.items() if len(olds) > 1}
    if clashes or many:
        print("rename_vendor: name clash, nothing changed", file=sys.stderr)
        for old in clashes:
            print("  %s -> %s exists" % (old, mapping[old]), file=sys.stderr)
        for new, olds in many.items():
            print("  %s <- %s" % (new, ", ".join(olds)), file=sys.stderr)
        return 1

    changed = 0
    for f, text in texts.items():
        out = TL_RE.sub(tl_sub, text)
        out = TYPEDEF_BLOCK.sub("", out)
        out = TYPE_RE.sub(lambda m: TYPES[m.group()], out)
        if out != text:
            changed += 1
            if not dry:
                with open(f, "w", encoding="utf-8", errors="surrogateescape") as fh:
                    fh.write(out)

    print("rename_vendor: %d identifiers, %d of %d files %s" %
          (len(mapping), changed, len(files), "would change" if dry else "changed"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
