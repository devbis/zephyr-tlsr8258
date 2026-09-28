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
# Usage: import_vendor.sh [--check] <tl_zigbee_sdk-dir> <libzigbee-dir>
#
# Without --check this refreshes the two import commits, nothing else. Later
# commits adapt some of the imported SDK files, so running this on an adapted
# tree reverts those adaptations: that is the intended flow. Re-run it, amend
# the matching import commit, then rebase the adaptation commits back on top.
#
# With --check nothing is copied: the tree is compared against both upstreams
# and every file that differs is listed, which on the import commits
# themselves must be none.
#
# Files present under the libzigbee directories here but in neither upstream
# are listed, not deleted: they were either removed upstream, and then need a
# "git rm", or added by a Zephyr commit. A new upstream file shows up as
# untracked and still needs adding to the build by hand.

set -eu

check_only=0
if [ $# -ge 1 ] && [ "$1" = "--check" ]; then
	check_only=1
	shift
fi

if [ $# -ne 2 ]; then
	echo "usage: $0 [--check] <tl_zigbee_sdk-dir> <libzigbee-dir>" >&2
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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mismatch=$work/mismatch
known=$work/known
: > "$mismatch"
: > "$known"

# tl_zigbee_sdk: listed files only, CRLF stripped. The last line of the list
# may lack a newline.
while IFS='|' read -r rel src || [ -n "$rel" ]; do
	[ -n "$rel" ] || continue
	echo "$rel" >> "$known"
	if [ ! -f "$sdk/$src" ]; then
		echo "$0: not in this SDK checkout, keeping the imported copy: $src" >&2
		continue
	fi
	sed 's/\r$//' "$sdk/$src" > "$work/sdk_file"
	if [ "$check_only" -eq 0 ]; then
		mkdir -p "$dest/$(dirname "$rel")"
		cp "$work/sdk_file" "$dest/$rel"
	elif ! cmp -s "$work/sdk_file" "$dest/$rel"; then
		echo "$rel" >> "$mismatch"
	fi
done < "$list"

# libzigbee: everything but its own SDK build fixture, byte for byte.
if [ "$check_only" -eq 0 ]; then
	rsync -a --exclude 'sdk/' "$lzb/src/" "$dest/"
fi

( cd "$lzb/src" && find . -type f -not -path './sdk/*' | sed 's|^\./||' ) > "$work/lzb_files"
while IFS= read -r rel; do
	echo "$rel" >> "$known"
	cmp -s "$lzb/src/$rel" "$dest/$rel" || echo "$rel" >> "$mismatch"
done < "$work/lzb_files"

# Files under the directories libzigbee provides that neither upstream has.
( cd "$lzb/src" && find . -mindepth 1 -maxdepth 1 -type d -not -name sdk |
  sed 's|^\./||' ) > "$work/lzb_dirs"
: > "$work/present"
while IFS= read -r d; do
	if [ -d "$dest/$d" ]; then
		( cd "$dest" && find "$d" -type f ) >> "$work/present"
	fi
done < "$work/lzb_dirs"
sort -u "$known" > "$work/known_sorted"
sort -u "$work/present" | comm -23 - "$work/known_sorted" > "$work/extra"
if [ -s "$work/extra" ]; then
	echo "in neither upstream (deleted upstream, or added by a Zephyr commit):" >&2
	sed 's/^/  /' "$work/extra" >&2
fi

if [ -s "$mismatch" ]; then
	echo "import is not verbatim:" >&2
	sed 's/^/  /' "$mismatch" >&2
	exit 1
fi
