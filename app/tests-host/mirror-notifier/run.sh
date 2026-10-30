#!/bin/sh
# Host test for the mirror notifier's 0xF4 packet (PATCH_MIRROR_MODE.md §6,
# MIRROR_PACKET_0xF4.md). Copies the packet code VERBATIM out of
# app/src/mirror_notifier.c (between its "mirror packet layout" markers), compiles it with
# host gcc and checks the bytes, plus the overlay's decode rule. Needs only gcc.
# Usage: sh app/tests-host/mirror-notifier/run.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
APP=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/mirror-notifier
mkdir -p "$OUT"
awk '/--- mirror packet layout:/{f=1;next} /--- end mirror packet layout ---/{f=0} f' \
    "$APP/src/mirror_notifier.c" > "$OUT/extracted_packet.c"
[ -s "$OUT/extracted_packet.c" ] || { echo "extraction failed: markers moved in mirror_notifier.c"; exit 1; }
gcc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-function -I "$OUT" "$HERE/harness.c" -o "$OUT/harness"
"$OUT/harness"
