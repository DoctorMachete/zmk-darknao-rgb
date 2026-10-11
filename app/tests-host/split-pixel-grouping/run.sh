#!/bin/sh
# Host test for the send-time pixel grouping (PATCH_ARCHITECTURE.md §5.4).
# Copies the grouping code VERBATIM out of app/src/split/bluetooth/central.c, compiles it
# against the real wire header with host gcc, and checks it against a model of the right
# half. Needs only gcc. Usage: app/tests-host/split-pixel-grouping/run.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
APP=$(cd "$HERE/../.." && pwd)
OUT=${TMPDIR:-/tmp}/split-pixel-grouping
mkdir -p "$OUT"
C="$APP/src/split/bluetooth/central.c"
awk '/^struct central_cmd_wrapper \{/,/^\};/' "$C" > "$OUT/extracted_wrapper.c"
awk '/^static struct central_cmd_wrapper split_run_carry;/{f=1} /^void split_central_split_run_callback/{f=0} f' "$C" > "$OUT/extracted_grouping.c"
[ -s "$OUT/extracted_grouping.c" ] || { echo "extraction failed: markers moved in central.c"; exit 1; }
gcc -std=gnu11 -O1 -Wall -Wextra -Wno-unused-function -Wno-address-of-packed-member \
    -I "$OUT" -I "$HERE/stub" -I "$APP/include" "$HERE/harness.c" -o "$OUT/harness"
"$OUT/harness"
