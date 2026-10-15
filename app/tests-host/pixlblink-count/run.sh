#!/bin/sh
# Host test for PIXLBLINK blink-count (PATCH_PIXLBLINK.md §2.3).
# Copies the slot timing code VERBATIM out of app/src/rgb_underglow.c (between its
# "pixlblink slot timing" markers), compiles it with host gcc, and checks phase and expiry
# frame by frame. Also checks the wire struct size against the real header.
# Needs only gcc. Usage: sh app/tests-host/pixlblink-count/run.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
APP=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/pixlblink-count
mkdir -p "$OUT"
awk '/--- pixlblink slot timing:/{f=1;next} /--- end pixlblink slot timing ---/{f=0} f' \
    "$APP/src/rgb_underglow.c" > "$OUT/extracted_slot_timing.c"
[ -s "$OUT/extracted_slot_timing.c" ] || { echo "extraction failed: markers moved in rgb_underglow.c"; exit 1; }
gcc -std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-function \
    -I "$OUT" -I "$HERE/../split-pixel-grouping/stub" -I "$APP/include" \
    "$HERE/harness.c" -o "$OUT/harness"
"$OUT/harness"
