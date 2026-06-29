# &pixel — minimal both-sides test (enter/exit listener version)

This build enables ONLY the &pixel behavior. The battery/USB indicator
behaviors are present in the tree but disabled (commented out in
app/CMakeLists.txt and app/dts/behaviors.dtsi), so they can't affect this build.

## 1. Apply the overlay
Extract go60-pixel-overlay.zip at the ROOT of your zmk-darknao-rgb fork.
The `app/...` paths overwrite/add exactly the changed files (20 files).

## 2. The pixel address header (now shipped inside the fork)
go60-pixels.h is included in the overlay at
app/include/dt-bindings/zmk/go60-pixels.h, so it lives on the same include path
as rgb.h / rgb_colors.h. Include it with ANGLE BRACKETS, matching every other
include in your keymap (none of your includes use quotes):

    #include <dt-bindings/zmk/go60-pixels.h>

Put this next to your existing `#include <dt-bindings/zmk/rgb.h>` line.
(Do NOT use `#include "go60-pixels.h"` — quoted/relative includes are not
reliably resolved by ZMK's devicetree preprocessor, especially under Nix.)

## 3. Address space

### Color names — IMPORTANT
Do NOT use the keymap's `RED_RGB` / `RED` / `GRN` style names in listeners. Those
are #define'd only inside the per-key-RGB section and #undef'd at its end, so in
the listener block (which comes after) they are UNDEFINED and will break the
build. Use either:
  - a raw literal:  `&pixel PIXEL_RH_C3R2 0x0000FF`
  - or the PX_* names added to go60-pixels.h (always in scope): `PX_RED`, `PX_BLUE`, ...
    plus `PX_OFF` (LED off, override kept). To remove an override use `(-1)`.

Global indices: 0..29 = LEFT half, 30..59 = RIGHT half. The &pixel behavior runs
on the central and relays right-half writes over the split automatically.

    &pixel PIXEL_LH_C3R2 PX_RED     // a left-half key LED red
    &pixel PIXEL_RH_C3R2 PX_GREEN    // a right-half key LED green
    &pixel PIXEL_LH_C3R2 (-1)        // clear that left pixel
    &pixel PIXEL_RH_C3R2 (-1)        // clear that right pixel

(0x000000 sets a pixel to OFF but KEEPS the override; (-1) removes it.)

## 3b. Clearing overrides (one binding)
Besides clearing a single pixel with `(-1)`, you can wipe a whole side or both at
once (the color parameter is ignored; pass 0):

    &pixel PIXEL_CLEAR_LEFT  0     // clear all LEFT-half custom colors
    &pixel PIXEL_CLEAR_RIGHT 0     // clear all RIGHT-half custom colors
    &pixel PIXEL_CLEAR_ALL   0     // clear both halves

These names are in go60-pixels.h. Handy on an `exit` to reset everything when
leaving a layer, e.g.:

    exit = <&pixel PIXEL_CLEAR_ALL 0>;

## 4. Listener test — this listener version uses enter / exit (chainable)
IMPORTANT: this is the `main` revision of zmk-listeners. It does NOT use the old
indexed `bindings = <set>, <clear>` form. Instead each listener has:
  - `enter` : one or more behaviors run on press/enter  (each pressed then released)
  - `exit`  : one or more behaviors run on release/exit
Both are chainable, so you light BOTH sides on enter and clear BOTH on exit in a
single node — no need for two nodes or a wrapper macro.

### Option A — keycode listener (dead-key probe)
Fires when a chosen keycode goes down (enter) and up (exit). Replace
YOUR_DEAD_KEY with a key the OS ignores (e.g. an unused F-key or a custom code).

    / {
        keycode_listeners {
            compatible = "zmk,keycode-listeners";

            pixel_probe {
                keycodes = <YOUR_DEAD_KEY>;
                enter = <&pixel PIXEL_LH_C3R2 PX_RED   &pixel PIXEL_RH_C3R2 PX_BLUE>;
                exit  = <&pixel PIXEL_LH_C3R2 (-1)      &pixel PIXEL_RH_C3R2 (-1)>;
            };
        };
    };

`keycodes` can list several keys; an optional `layers = <...>` restricts the
listener to those layers (defaults to all layers).

### Option B — layer listener (matches how your keymap already works)
Light both sides while a layer is active; clear when leaving it. This mirrors the
`rgb_on_off` / `switch_testing` nodes already in your keymap.

    / {
        layer_listeners {
            compatible = "zmk,layer-listeners";

            pixel_probe_layer {
                layers = <LAYER__SWITCH_TESTING>;   // any layer you can toggle
                enter = <&pixel PIXEL_LH_C3R2 PX_RED   &pixel PIXEL_RH_C3R2 PX_BLUE>;
                exit  = <&pixel PIXEL_LH_C3R2 (-1)      &pixel PIXEL_RH_C3R2 (-1)>;
            };
        };
    };

(If you want it to fire only once for a set of layers treated as a unit, add
`group-of-layers;` like your `rgb_on_off` node does.)

## What to look for
- LEFT LED lights on enter, clears on exit  -> central path OK.
- RIGHT LED lights on enter, clears on exit  -> SPLIT path OK (the goal).
- RIGHT LED lights but at the WRONG position -> PIXEL_RH_* mapping needs a tweak
  (it was derived from the pixel-lookup table; tell me which key vs which LED lit).
- RIGHT LED never lights -> confirm BOTH halves were flashed with this build and
  reconnected. See the pairing note below.

## Pairing note (only if the right side stays dark)
First just flash both halves and let them power-cycle and reconnect normally —
that re-runs GATT discovery and the central should pick up the new pixel
characteristic. Do NOT reset anything up front. ONLY if the left side works but
the right side never responds is a settings/bond reset worth trying (stale GATT
service cache). If neither side works, it's a build/flash issue, not pairing.

## Notes
- Pixels persist across RGB on/off until cleared.
- Right-half indices are NOT contiguous in key order; always use PIXEL_RH_* names.
- tap-ms / wait-ms on the listener root control press->release and inter-binding
  timing (default 5 ms each); the defaults are fine for pixels.
