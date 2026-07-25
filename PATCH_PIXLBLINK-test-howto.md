# &pixlblink — minimal both-sides test

Bench procedure to confirm PIXLBLINK once it is implemented in the fork. Assumes
`&pixel` already works on this tree (it is the sibling behavior and shares the
addressing + split path).

## 0. Spec under test (v001)
- Sharp square-wave blink between one color and BLACK.
- Frequency hardcoded in C: `code = 5` → **0.5 Hz** → ~1000 ms ON, ~1000 ms OFF.
- `&pixlblink <global_pos> <color>`; `<color>` negative stops/clears the blink.

## 1. Build / flash
- This is a fork source change. Rebuild the fork and **bump the SHA pin in
  `west.yml`** before building the keymap. Flash BOTH halves.

## 2. Address space (same as &pixel)
- `0 .. local_count-1`  = LEFT (central) strip.
- `>= local_count`      = RIGHT (peripheral) strip (relayed as `pos - local_count`).
- Use `PIXEL_LH_*` / `PIXEL_RH_*` names from go60-pixels.h. Never raw `+1` math.

## 3. Color names — IMPORTANT
- Use `PX_*` (e.g. `PX_RED`, `PX_BLUE`) or a raw `0xRRGGBB`.
- The keymap's `*_RGB` names are `#undef`'d before the listener block; they will
  not resolve inside these bindings.

## 4. Listener test (enter / exit — chainable, both halves)
Pick a spare layer and add:
```
    blink_test {
        layers = <LAYER__SOMELAYER>;
        not-press-release-outputs;
        enter = <&pixlblink PIXEL_LH_C6R4 PX_RED   &pixlblink PIXEL_RH_C6R4 PX_RED>;
        exit  = <&pixlblink PIXEL_LH_C6R4 (-1)     &pixlblink PIXEL_RH_C6R4 (-1)>;
    };
```
Enter the layer → both named pixels should blink red at ~0.5 Hz. Leave the layer
→ both stop and return to underglow/`&pixel`.

## What to look for
- **Both** LEFT and RIGHT pixels blink, roughly in step (minor L/R drift over time
  is expected in v001 — independent clocks, no phase sync).
- ON phase is the chosen color; OFF phase is fully dark.
- ~1 s on / ~1 s off. If it is twice/half that, re-check the `Hz = code/10` math
  and the 25 ms tick assumption.

## Critical check — blink while RGB underglow is OFF
Turn RGB underglow OFF, then trigger a blink.
- **Expected:** it still blinks (the ext-power gate includes `any_pixlblink`, and
  the render tick stays alive while blinking).
- **If nothing lights:** the ext-power gate is missing `any_pixlblink` (or the tick
  was stopped in `underglow_off`). This is the exact MAGIC-layer failure — fix in
  `zmk_rgb_set_ext_power()` / the tick lifecycle, not in the keymap.

## Right side stays dark (only if LEFT works)
- Confirm the peripheral `switch(op)` in `service.c` handles the new
  `OP_PIXLBLINK` / `OP_PIXLBLINK_CLEAR` ops (a missing case = remote blink ignored
  while local works).
- Confirm both halves are on the SAME firmware build (reflash both).

## Stopping vs. black
- `&pixlblink <pos> (-1)` truly stops and clears.
- `&pixlblink <pos> 0x000000` blinks black↔black = looks like nothing; it is NOT a
  stop. Always clear with `(-1)`.
