# PIXLBLINK v002a — split struct packing FIX (right-half regression)

## Symptom
After the v002 preset overlay, the RIGHT (peripheral) half stopped responding to
BOTH `&pixlblink` presets AND the pre-existing `&pixel` (e.g. `caps_feedback`
`&pixel PIXEL_RH_C1R1 PX_RED`). Left half fine. This was a regression: right-half
`&pixel` worked in every prior version.

## Root cause (my bug, introduced in v002)
The split RGB command is defined twice: the wire struct `set_rgb_pixel` (a NAMED
member inside a `__packed` union in `types.h`) and the peripheral's local
`rgb_pixel_msg` (in `service.c`). The peripheral receives with a raw
`memcpy(&msg, buf, MIN(len, sizeof(msg)))`, so the two structs must have identical
byte offsets.

GCC's `__packed` on the OUTER union does NOT propagate into a named nested struct
member. So the central actually writes `set_rgb_pixel` with natural alignment
(`color` at offset 4). Every working version paired that with an UNPACKED local
`rgb_pixel_msg` (also `color` at offset 4) — matched.

In v002 I added `__packed` to the peripheral's local `rgb_pixel_msg` (mistakenly
believing it fixed a latent mismatch). That moved the local `color` to offset 3,
so the peripheral read `color`/`position` from the wrong bytes and misparsed EVERY
RGB pixel command → all right-half pixel ops broke.

## Fix
Remove `__packed` from the peripheral's local `rgb_pixel_msg` in
`app/src/split/bluetooth/service.c`. With natural alignment on both sides, all
fields line up:

    field      central-writes(offset)   peripheral-reads(offset)
    op         0                        0
    position   1                        1
    count      2                        2
    color      4                        4
    positions  8                        8
    color2     16                       16
    freq_code  20                       20

Both structs are 24 bytes; the central writes 24, the peripheral copies 24, so the
new color2/freq_code fields arrive intact AND color/position return to the offsets
that made &pixel work on the right half.

## Verification (inspection)
- Standalone offset computation confirms central-write offsets == peripheral-read
  offsets for every field (including the pre-existing op/position/count/color).
- Brace/paren balance OK on service.c.
- The ONLY change from the v002 overlay is one word: `struct __packed
  rgb_pixel_msg` -> `struct rgb_pixel_msg`.

## Apply
Reflash BOTH halves with this build (your merged both-sides image is fine).
The single changed file vs v002 is `app/src/split/bluetooth/service.c`.
