# Relocatable BLE/USB status indicators (`&magic_indicator_*`)

## Goal
Show the **native** Magic-layer BLE/USB connection indicators (4× BLE + 1× USB)
at **arbitrary keymap positions on either half**, driven by the **layer
listener** (enter → show, exit → hide). Colors are **not** author-chosen: they
reuse the exact native state→color ladder so the relocated dot behaves
identically to the built-in Magic indicator, updating **live** while the layer
is held.

The existing `&pixel` behavior is **unchanged** (still arbitrary static colors).

## Why this turned out small
Three things already existed in the fork and are reused wholesale:

1. **Cross-half color routing** — `behavior_pixel_color.c` already runs on the
   central (GLOBAL locality), splits the global address space (0..29 = left
   local, 30..59 = right via `position - local_count`), and pushes resolved
   colors to the peripheral with `zmk_split_central_set_pixel(pos, color)`.
   The peripheral **holds** that color in `pixel_overrides[]` (op `OP_SET` in
   `split/bluetooth/service.c`) and repaints it every frame. So a pushed dot
   stays lit between updates — re-push only on actual state change.

2. **Native color ladders** — already implemented in `rgb_underglow.c`:
   - BLE (per profile index `i`):
     - status==2 AND active endpoint is BLE AND active profile==i → **white**
     - status==2 → **dull_green** (0x00ff68)
     - status==1 → **red**       (0xff0000)
     - status==0 → **lilac**     (0x6b1fce)
   - USB:
     - HID conn AND active endpoint USB → **white**
     - HID conn → **dull_green**
     - powered  → **red**
     - none     → **lilac**

3. **Change events** — `ble_active_profile_changed`, `endpoint_changed`,
   `usb_conn_state_changed`, with an existing `ZMK_LISTENER/ZMK_SUBSCRIPTION`
   pattern in `rgb_underglow.c` to copy.

## Key architectural decision: push **resolved color**, not a "BLE op"
The peripheral **cannot** compute BLE/USB color — endpoint/profile state is a
central-only concept (`zmk_endpoints_selected()` doesn't exist on the
peripheral; the fork's own `OP_USB` path is a documented no-op there). So we do
NOT add an `OP_BLE`/per-profile sync. Instead the **central** computes the
native color and pushes it as a plain resolved-RGB `OP_SET` — exactly the
`&pixel` path. No new split opcode, no peripheral changes.

## Parameters (your design)
- `&magic_indicator_on  POSITION WHICH`  — param1 = global pixel position
  (same address space as `&pixel`, so `PIXEL_RH_C1R2` etc. work), param2 =
  WHICH ∈ {`MAGIC_USB`, `MAGIC_BLE0`..`MAGIC_BLE4`}.
- `&magic_indicator_off POSITION WHICH`  — clears that pixel; also
  deregisters it from the live-update set.
- `&magic_indicator_clear`               — clears ALL active indicators
  (both halves) and empties the set.

## Live updating
A small **registry** on the central tracks active indicators
`{position, which}` (max 8). On enter, the behavior registers + paints. A
single `ZMK_LISTENER` subscribed to the three change events recomputes every
registered indicator's color and re-applies it (local → `set_pixel` /
peripheral → `zmk_split_central_set_pixel`). On exit/clear, entries are removed
and pixels cleared. Local-half dots would also be repainted by the native
render loop, but the registry handles both halves uniformly so behavior is
identical left vs right.

## Files
- `app/include/dt-bindings/zmk/magic-indicator.h` — `MAGIC_USB`, `MAGIC_BLE0..4`.
- `app/include/zmk/magic_indicator.h` — central registry API.
- `app/src/magic_indicator.c` — registry, color resolution (reuses ladders),
  routing, and the change-event listener.
- `app/src/behaviors/behavior_magic_indicator.c` — the on/off behavior.
- `app/src/behaviors/behavior_magic_indicator_clear.c` — the clear-all behavior.
- `app/dts/bindings/behaviors/zmk,behavior-magic-indicator.yaml`
- `app/dts/bindings/behaviors/zmk,behavior-magic-indicator-clear.yaml`
- `app/src/CMakeLists.txt` / behaviors CMake — add the new sources.
- Keymap: behavior nodes + a layer-listener node (example included).

## Color source-of-truth note
The color ladders are currently inline in `rgb_underglow.c`. To guarantee the
relocated dot never drifts from the native one, this implementation factors the
two ladders into small shared helpers
(`zmk_magic_indicator_ble_color(i)` / `_usb_color()`) that BOTH the native
render path and the new code call. That means future color tweaks change both
in lockstep. (If you'd rather not touch `rgb_underglow.c`'s native path, the
helpers can instead be private copies — but then the two can drift. Factoring
is the maintainable choice and is what's implemented.)
