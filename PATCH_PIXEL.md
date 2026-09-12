# PATCH — `&pixel` (per-pixel RGB override)

**Status: working on hardware, both halves.** The foundation the MAGIC and
PIXLBLINK layers build on.

Shared background (composite stack, split transport, addressing, constraints):
`PATCH_ARCHITECTURE.md`.

---

## 1. What it does

Sets any individual LED on **either** half to an arbitrary `0xRRGGBB` colour,
persistently, **compositing over** the underglow / per-layer RGB rather than
replacing it, and regardless of whether RGB is on or off (it drives the strip and
ext-power on demand).

## 2. API

```
compatible = "zmk,behavior-pixel-color"
#binding-cells = <2>
locality       = BEHAVIOR_LOCALITY_GLOBAL
```

| param | meaning |
|---|---|
| `param1` | **global** strip position — `0..29` LEFT, `30..59` RIGHT (relayed as `pos − 30`) |
| `param2` | colour `0xRRGGBB`; **negative clears** the override |

Node is declared by the fork in `app/dts/behaviors/pixel_color.dtsi` as `&pixel`.

### 2.1 Clear semantics — the distinction that trips people up
- **`(-1)` removes the override** → the pixel returns to the layer/underglow beneath.
- **`0x000000` sets the LED black but KEEPS the override** → still owned by `&pixel`.

These are not the same thing. Use `(-1)` when you mean "stop overriding".

### 2.2 Group clears
Pass the sentinel as `param1` with colour `0` (colour is ignored):

| sentinel | value | effect |
|---|---|---|
| `PIXEL_CLEAR_LEFT` | 250 | clear all LH overrides |
| `PIXEL_CLEAR_RIGHT` | 251 | clear all RH overrides |
| `PIXEL_CLEAR_ALL` | 252 | clear both halves |

These mirror `ZMK_PIXEL_CLEAR_*` in `rgb_underglow.h` — keep the two in sync.

> Group clears also wipe the MAGIC layer (3 lines in `clear_pixels()`). They are
> **not** guaranteed to stop PIXLBLINK blinks — only `&pixlblink_off` is. See
> `PATCH_PIXLBLINK.md`.

## 3. Firmware model

- Buffers `pixel_overrides[]` / `pixel_override_active[]` + the `any_pixel_override`
  fast-path flag.
- Stamped in `zmk_led_generate_status()` above the layer RGB, below PIXLBLINK and
  MAGIC.
- Colours are brightness-scaled by `CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX`.
- API in `rgb_underglow.h`: `zmk_rgb_underglow_set_pixel()`,
  `zmk_rgb_underglow_clear_pixels()`, `zmk_rgb_underglow_pixel_count()`.
- **Compositing note (historic "Runtime #3"):** overrides are stamped into
  `status_pixels[]` with a `status_overlay_active[]` mask covering only the
  positions written this frame. **Do not reintroduce a forced `blend = 256` for
  overrides** — that was the bug where one override blacked out the whole strip.

### Split
Uses `ZMK_SPLIT_RGB_PIXEL_OP_SET` / `OP_CLEAR_ONE` / `OP_CLEAR_ALL` on the shared
pixel channel. The peripheral stores pushed colours in its own `pixel_overrides[]`
and repaints every frame. Full transport detail and the **20-byte payload ceiling**
in `PATCH_ARCHITECTURE.md` §5.

## 4. Files

**New:** `app/src/behaviors/behavior_pixel_color.c`,
`app/dts/behaviors/pixel_color.dtsi`,
`app/dts/bindings/behaviors/zmk,behavior-pixel-color.yaml`.

**Modified:** `app/src/rgb_underglow.c` (+ `.h`), the split transport
(`central.c`/`.h`, `bluetooth/central.c`, `bluetooth/service.c`, `peripheral.c`,
`wired/central.c`, `transport/types.h`, `bluetooth/uuid.h`),
`app/CMakeLists.txt`, `app/dts/behaviors.dtsi`.

## 5. Keymap usage

Declared by the fork — just use it. Inside a layer listener:

```dts
caps_feedback {
    layers = <LAYER__CAPS>;
    not-press-release-outputs;
    enter = <&pixel PIXEL_LH_C1R1 PX_RED   &pixel PIXEL_RH_C1R1 PX_RED>;
    exit  = <&pixel PIXEL_LH_C1R1 (-1)     &pixel PIXEL_RH_C1R1 (-1)>;
};
```

Or bound to a key: `&pixel PIXEL_LH_C6R4 PX_PINK`.

**Reminders:** use `PX_*` or raw `0xRRGGBB` (the keymap's `*_RGB` names are
`#undef`'d before the listener block); `POS_*` are not strip indices; reflash both
halves after firmware changes.

## 6. Bench test

1. Pick a spare layer; add a listener that lights one LH and one RH pixel on enter
   and clears both with `(-1)` on exit.
2. Enter the layer → both light. Leave → both return to underglow.
3. **With RGB underglow OFF** → they must still light (ext-power is driven on
   demand). If not, the ext-power gate lost `any_pixel_override`.
4. **RH dark while LH works** → check the split. If per-layer RGB still works on the
   RH but *no* pixel op does, suspect the payload-size ceiling
   (`PATCH_ARCHITECTURE.md` §5.1), not the pixel code.
5. Wrong RH pixel lights → fix the `PIXEL_RH_*` value in `go60-pixels.h`.

## 7. Secondary indicators (battery / USB) — PRESENT BUT DISABLED

Relocatable battery-block and USB-output indicator behaviors exist in the tree but
are **commented out** in both `app/CMakeLists.txt` and `app/dts/behaviors.dtsi`, so
they cannot affect a build. Largely superseded by MAGIC (which also covers the
right half). The battery block remains available if relocatable battery is wanted.

- `zmk,behavior-battery-indicator` — `positions` array (local half, up to 8);
  param1 = 1 show / 0 hide.
- `zmk,behavior-usb-indicator` — single `position`; param1 = 1 show / 0 hide.
- Backing API: `set/clear_battery_indicator`, `set/clear_usb_indicator` in
  `rgb_underglow.h`. Their `persist_battery.active` / `persist_usb.active` flags are
  what `any_persist_indicator()` reports — which is why that function does **not**
  cover the MAGIC or PIXLBLINK layers.

**To re-enable:** uncomment the two `target_sources_ifdef` lines in
`app/CMakeLists.txt` and the `#include <behaviors/status_indicators.dtsi>` in
`app/dts/behaviors.dtsi`.

## 8. Open items
- [ ] Decide whether to re-enable or delete the secondary battery/USB indicators.
- [ ] Confirm whether `PIXEL_CLEAR_*` should also stop PIXLBLINK blinks.
