# PIXLBLINK v002b — ATT MTU payload-size FIX (right half)

## Symptom
Right (peripheral) half ignored ALL pixel commands — both `&pixlblink` presets and
the pre-existing `&pixel` (e.g. `caps_feedback`). Left half fine. Per-layer RGB
colours still worked on the right, and keys worked, proving the peripheral was
running the new firmware and the split link was healthy.

## Root cause
`bt_gatt_write_without_response()` is capped at **ATT_MTU - 3 = 20 bytes**
(Zephyr default MTU 23; this fork sets no MTU override anywhere).

ZMK sizes its split payloads to respect that — `zmk_split_run_behavior_payload` is
`__packed` and exactly 20 bytes, which is why `ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN` is 9.

Payload sizes:

    run_behavior (keys)      20 bytes  fits
    set_rgb_layers (layer RGB) 4 bytes  fits   <- why RNPAD colours still worked
    set_rgb_pixel  v001      16 bytes  fits   <- &pixel worked on the right
    set_rgb_pixel  v002/a    24 bytes  TOO BIG <- the bug

Adding `color2` + `freq_code` pushed `set_rgb_pixel` to 24 bytes. Every RGB pixel
write was rejected by the BLE stack before transmission, so nothing reached the
peripheral — killing `&pixel` and `&pixlblink` together.

## Fix
`positions[]` (battery op) and the blink extras are never used by the same op, so
they now share storage in a union. The payload returns to **16 bytes**, identical
to the version that worked:

    op@0  position@1  count@2  color@4
    union u { positions[8]@8 | blink{ color2@8, freq_code@12 } }   -> size 16

Verified by compiling the real structs from this tree: wire and peripheral structs
both 16 bytes with identical offsets, and 16 <= 20 so the write is accepted.

## Files changed (4)
- `app/include/zmk/split/transport/types.h` — union layout + a comment warning that
  this struct must stay <= 20 bytes.
- `app/src/split/bluetooth/service.c` — mirrored struct + call sites (`msg.u.*`).
- `app/src/split/central.c` — sender sites (`.u.positions`, `.u.blink.*`).
- `app/src/split/peripheral.c` — `.u.positions`, plus the two PIXLBLINK ops added
  to the generic transport dispatch (they were missing there).

## Rule going forward
**Never let `set_rgb_pixel` exceed 20 bytes.** If more per-blink state is ever
needed, either overlap it into the union or raise `CONFIG_BT_L2CAP_TX_MTU` on BOTH
halves (which changes the split ATT negotiation and needs its own testing).
