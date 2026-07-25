# PIXLBLINK — fork implementation manifest (v001)

Implements `&pixlblink` on top of `zmk-darknao-rgb-rgb-layer-24.12C`, per
`PATCH_PIXLBLINK_DESIGN_v001.md`. Sharp square-wave blink of one pixel between a
color and black, on either half, at a fixed firmware frequency (0.5 Hz).
The static `&pixel` behavior is untouched.

## How to apply
Unzip `PATCH_PIXLBLINK_patch_v001.zip` (contains `pixlblink-overlay/app/...`) and
copy its `app/` tree over the root of the fork, OR apply
`PATCH_PIXLBLINK_changes_v001.diff` to the 8 modified files and add the 3 new
files. Then rebuild the fork and **bump the SHA pin in `west.yml`**.

## New files (3)
- `app/src/behaviors/behavior_pixlblink.c` — behavior; GLOBAL locality; param1=pos,
  param2=color(neg clears); routes local→`zmk_rgb_underglow_set_pixlblink`,
  remote→`zmk_split_central_set/clear_pixlblink`. Clone of `behavior_pixel_color.c`.
- `app/dts/behaviors/pixlblink.dtsi` — declares `pixlblink: pixlblink`
  (`compatible = "zmk,behavior-pixlblink"`, `#binding-cells = <2>`).
- `app/dts/bindings/behaviors/zmk,behavior-pixlblink.yaml` — `two_param` binding.

## Modified files (8)
1. `app/include/zmk/rgb_underglow.h` — declare `zmk_rgb_underglow_set_pixlblink()`;
   add `PIXLBLINK_FREQ_CODE` (default 5 = 0.5 Hz; `freq_hz = code/10`).
2. `app/src/rgb_underglow.c` —
   - new layer buffers `pixlblink_color[]` / `pixlblink_active[]` / `any_pixlblink`
     + stateless phase helper `pixlblink_on_phase()` (reads `k_uptime_get()`).
   - `zmk_led_write_pixels()` gates: `any_pixlblink` added to `persist_overlay`
     and the blend-trigger condition.
   - `zmk_led_generate_status()`: blink stamped BETWEEN `&pixel` overrides and
     magic indicators (priority: underglow → &pixel → PIXLBLINK → magic). OFF
     phase stamps black + sets the overlay mask (drives the pixel dark).
   - **`zmk_rgb_set_ext_power()` gate: `any_pixlblink` added** (the MAGIC-layer
     bug, fixed preemptively — powers the strip for a blink when underglow is off).
   - `zmk_rgb_underglow_status_update()`: refresh timer stays alive while
     `any_pixlblink` (this 25 ms timer is the blink's animation clock).
   - new setter `zmk_rgb_underglow_set_pixlblink()` — records/clears the ON color,
     recomputes `any_pixlblink`, powers + paints, and starts the refresh timer.
3. `app/include/zmk/split/transport/types.h` — ops
   `ZMK_SPLIT_RGB_PIXEL_OP_PIXLBLINK = 7`, `..._PIXLBLINK_CLEAR = 8`
   (no new payload fields — position+color suffice; freq identical both sides).
4. `app/include/zmk/split/central.h` — declare
   `zmk_split_central_set_pixlblink()` / `_clear_pixlblink()`.
5. `app/src/split/central.c` — implement those two senders (mirrors set/clear pixel).
6. `app/src/split/bluetooth/service.c` — peripheral `switch(op)`: handle the two
   new ops → `zmk_rgb_underglow_set_pixlblink(pos, color / -1)`.
7. `app/CMakeLists.txt` — register `behavior_pixlblink.c` under
   `CONFIG_ZMK_RGB_UNDERGLOW`.
8. `app/dts/behaviors.dtsi` — `#include <behaviors/pixlblink.dtsi>`.

## Verification done (inspection only — NOT a Zephyr build)
- Brace/paren balance OK on all edited C files.
- Standalone `gcc -Wall -Wextra` compile of the isolated phase math + stamping +
  setter core: color `0xFF8000` → `(100,50,0)` at BRT_MAX=100; phase at 0.5 Hz
  gives ON@t=0, OFF@t=1000ms, ON@t=2000ms (1 s on / 1 s off); `-1` clears and
  resets `any_pixlblink`. All correct.
- Designated initializers use the file's existing GNU colon-form.
- Declaration/use ordering checked (buffers/helper before uses; timer before setter).

## First-build / first-flash watch items
1. Confirm `any_pixlblink` is in the ext-power gate (done here) — else invisible
   when RGB off.
2. Confirm the refresh timer stays alive while blinking (done here) — else it
   paints one phase then freezes.
3. Peripheral op cases present (done) — else remote blink ignored.
4. `&pixlblink` node declared + CMake registration (done) — else DT parse fails
   and the whole keymap won't build.

## Keymap
Not included here (fork only, as requested). Use
`PATCH_PIXLBLINK_KEYMAP_SNIPPET_v001.md` to declare the node and wire it.
Frequency is not a keymap parameter in v001 — change `PIXLBLINK_FREQ_CODE` in the
fork and rebuild to alter the rate.
