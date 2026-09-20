#!/bin/sh
# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
#
# Refresh the vendor imports under subsys/zigbee.
#
# Two upstreams feed this subsystem and both are imported verbatim, so a
# refresh is a copy followed by "git commit --amend" on the matching import
# commit rather than a merge:
#
#   tl_zigbee_sdk  the open part of the Telink Zigbee SDK (ZCL, Green Power,
#                  AF, BDB, OTA, WWAH and the SDK common/OS headers). Only the
#                  files listed in vendor-sdk-files.txt are imported, and the
#                  sole transformation is CRLF to LF.
#                  One listed file, aps/aps_stackUse.h, comes from a newer
#                  vendor release than the rest; it is skipped with a warning
#                  when the given checkout does not carry it.
#   libzigbee      the reconstructed closed part of the stack (MAC, NWK, APS,
#                  security service, ZDO, buffers, task queue). Imported byte
#                  for byte; libzigbee owns the layout and the coding style.
#
# Usage: import_vendor.sh <tl_zigbee_sdk-dir> <libzigbee-dir>
#
# This refreshes the two import commits, nothing else. Later commits adapt some
# of the imported SDK files, so running this on an adapted tree reverts those
# adaptations: that is the intended flow. Re-run it, amend the matching import
# commit, then rebase the adaptation commits back on top.
#
# Afterwards "git status" must show only content changes: a new file upstream
# that shows up as untracked still needs adding to the build by hand.

set -eu

if [ $# -ne 2 ]; then
	echo "usage: $0 <tl_zigbee_sdk-dir> <libzigbee-dir>" >&2
	exit 2
fi

sdk=$1
lzb=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
zephyr_base=$(CDPATH= cd -- "$here/../.." && pwd)
dest=$zephyr_base/subsys/zigbee
list=$here/vendor-sdk-files.txt

for d in "$sdk/zigbee" "$lzb/src"; do
	if [ ! -d "$d" ]; then
		echo "$0: no such directory: $d" >&2
		exit 1
	fi
done

mismatch=$(mktemp)
trap 'rm -f "$mismatch"' EXIT

# tl_zigbee_sdk: listed files only, CRLF stripped.
while IFS='|' read -r rel src; do
	[ -n "$rel" ] || continue
	if [ ! -f "$sdk/$src" ]; then
		echo "$0: not in this SDK checkout, keeping the imported copy: $src" >&2
		continue
	fi
	mkdir -p "$dest/$(dirname "$rel")"
	sed 's/\r$//' "$sdk/$src" > "$dest/$rel"
	sed 's/\r$//' "$sdk/$src" | cmp -s - "$dest/$rel" || echo "$rel" >> "$mismatch"
done < "$list"

# libzigbee: everything but its own SDK build fixture, byte for byte.
rsync -a --exclude 'sdk/' "$lzb/src/" "$dest/"

( cd "$lzb/src" && find . -type f -not -path './sdk/*' | sed 's|^\./||' ) |
while read -r rel; do
	cmp -s "$lzb/src/$rel" "$dest/$rel" || echo "$rel" >> "$mismatch"
done

if [ -s "$mismatch" ]; then
	echo "import is not verbatim:" >&2
	sed 's/^/  /' "$mismatch" >&2
	exit 1
fi
