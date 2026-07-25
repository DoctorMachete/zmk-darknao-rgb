# PIXLBLINK — preset two-color blink behavior (`&pixlblink` + `&pixlblink_off`)

> **ARTIFACT VERSIONS (this set):**
> - `PATCH_PIXLBLINK_DESIGN_v002.md` (this file)
> - `PATCH_PIXLBLINK_HANDOFF_v002.md`
> - `PATCH_PIXLBLINK_KEYMAP_SNIPPET_v002.md`
> - `PATCH_PIXLBLINK-test-howto.md`
> - `PATCH_PIXLBLINK_IMPL_MANIFEST_v002.md`
>
> **v002 supersedes v001.** v001 was a single global `&pixlblink POS COLOR` (2
> cells, color↔black, one hardcoded frequency). v002 is the **preset model**:
> named instances each carry two colors + a frequency as DT properties and are
> invoked with **one** parameter (the position), plus a fixed `&pixlblink_off`.
> The v001 2-cell form is **removed** (Option A migration).
>
> Naming convention unchanged: `PATCH_` + feature token (`PIXLBLINK`, no `E`) +
> doc type + `vNNN`; functional files (`west.yml`) never prefixed/versioned.

---

## What changed vs v001
| aspect            | v001                              | v002 (this)                                  |
|-------------------|-----------------------------------|----------------------------------------------|
| invocation        | `&pixlblink POS COLOR` (2 cells)  | `&<preset> POS` (1 cell)                     |
| colors            | one color ↔ black                 | color1 ↔ color2 (either may be black)        |
| frequency         | one hardcoded global (0.5 Hz)     | per-instance DT property (per-position rate) |
| clear             | `&pixlblink POS (-1)`             | `&pixlblink_off POS` (fixed behavior)        |
| behavior instances| single                            | many named presets + one fixed off           |

---

## Goal
Let the author define reusable blink **presets** in the keymap, each with its own
two colors and frequency, and drop them onto pixels by position:
```
blink_recording_macro: blink_recording_macro {
    compatible = "zmk,behavior-pixlblink";
    #binding-cells = <1>;
    frequency = <5>;            // freq_hz = code / 10  -> 0.5 Hz
    color1 = <PX_ORANGE>;
    color2 = <PX_RED>;          // optional; defaults to black (off half-cycle)
};
// ... &blink_recording_macro PIXEL_LH_C3R2   &blink_recording_macro PIXEL_LH_C4R2 ...
```
and stop any pixel with the fixed, non-configurable companion:
```
&pixlblink_off PIXEL_LH_C3R2
```
`&pixel` and the magic-indicator layer are untouched.

---

## Two behaviors
### 1. `zmk,behavior-pixlblink` — the preset (user-defined, multi-instance)
- `#binding-cells = <1>` (param1 = GLOBAL position).
- DT properties: `color1` (default `0xFFFFFF`), `color2` (default `0x000000`),
  `frequency` (default `5`). All optional; a minimal instance with only `color1`
  blinks that color ↔ black at 0.5 Hz (the v001 behavior, as a preset).
- Implemented with the standard multi-instance pattern (`DT_INST_FOREACH_STATUS_OKAY`
  + a per-instance `behavior_pixlblink_config` built from `DT_INST_PROP`). The
  handler reads the invoked instance's config via
  `zmk_behavior_get_binding(binding->behavior_dev)->config` — same mechanism the
  fork's `sticky_key` uses. GLOBAL locality.
- The driver compiles in as soon as ≥1 instance with this compatible exists in the
  merged devicetree (the keymap counts). If the keymap defines no presets, the
  driver simply compiles out.

### 2. `zmk,behavior-pixlblink-off` — the fixed off (NOT user-configurable)
- `#binding-cells = <1>` (param1 = GLOBAL position).
- Single instance shipped by the fork in `app/dts/behaviors/pixlblink.dtsi`
  (`&pixlblink_off`). Stops the blink at the position, revealing whatever is
  beneath. GLOBAL locality. Calls `zmk_rgb_underglow_clear_pixlblink()`.

---

## Firmware data model (rgb_underglow.c)
Per-position slot instead of a single color buffer:
```c
struct pixlblink_slot {
    struct led_rgb color1;  // ON half-cycle
    struct led_rgb color2;  // OFF half-cycle (may be black)
    uint32_t half_ms;       // 5000 / freq_code  (code 5 -> 1000 ms)
    bool active;
};
static struct pixlblink_slot pixlblink_slots[STRIP_NUM_PIXELS];
static bool any_pixlblink;
```
- **Per-position phase** (each slot uses its OWN half_ms, so pixels can blink at
  different rates): in the composite, `on = ((k_uptime_get() / slot.half_ms) & 1) == 0`,
  stamp `color1` if on else `color2`, set the overlay mask either way (color2 may
  be a real color, not just black).
- **Setter** `zmk_rgb_underglow_set_pixlblink(pos, color1, color2, freq_code)` —
  scales both colors (BRT_MAX, like `&pixel`), stores half_ms, powers the strip,
  paints, and starts the 25 ms refresh timer.
- **Clear** `zmk_rgb_underglow_clear_pixlblink(pos)` — zeroes the slot, recomputes
  `any_pixlblink`, repaints.
- Composite priority unchanged: underglow → `&pixel` → **PIXLBLINK** → magic.

### Carried-over gotchas (still handled)
- `any_pixlblink` is in the **ext-power gate** (blink visible when underglow off).
- The **refresh timer stays alive** while `any_pixlblink` (animation clock).

---

## Split model (both halves animate autonomously)
The peripheral gets the full spec once, then animates locally.
- Payload `set_rgb_pixel` gains **`uint32_t color2`** and **`uint8_t freq_code`**
  (alongside the existing `color`, reused as color1). Ops unchanged
  (`OP_PIXLBLINK` = 7, `OP_PIXLBLINK_CLEAR` = 8).
- Central sender `zmk_split_central_set_pixlblink(pos, color1, color2, freq_code)`;
  clear unchanged.
- Peripheral `service.c` copies the wider struct and calls
  `set_pixlblink(pos, color, color2, freq_code)` / `clear_pixlblink(pos)`.
- **Layout note (important):** the peripheral's local `rgb_pixel_msg` struct is now
  `__packed` to match the packed wire struct byte-for-byte (the transport does a
  bulk `memcpy` of `sizeof(set_rgb_pixel)`). This also corrects a latent field-
  offset mismatch that existed in v001 for the `color` field.

---

## Files (v002)
**New:** `app/src/behaviors/behavior_pixlblink_off.c`,
`app/dts/bindings/behaviors/zmk,behavior-pixlblink-off.yaml`.
**Rewritten:** `app/src/behaviors/behavior_pixlblink.c` (multi-instance preset),
`app/dts/behaviors/pixlblink.dtsi` (ships `&pixlblink_off`, documents preset shape),
`app/dts/bindings/behaviors/zmk,behavior-pixlblink.yaml` (one_param + color/freq props).
**Modified:** `app/src/rgb_underglow.c` (slot model + two-arg-color setter + clear),
`app/include/zmk/rgb_underglow.h` (new signatures, `PIXLBLINK_DEFAULT_FREQ_CODE`),
`app/include/zmk/split/transport/types.h` (+color2/+freq_code),
`app/include/zmk/split/central.h` + `app/src/split/central.c` (sender signature),
`app/src/split/bluetooth/service.c` (packed msg + wider handler),
`app/CMakeLists.txt` (register the off behavior).

---

## Keymap migration (Option A — breaking, one-time)
Any v001 usage must migrate:
- `&pixlblink POS 0xRRGGBB` → define a preset (`color1 = <0xRRGGBB>;` etc.) and use
  `&<preset> POS`.
- `&pixlblink POS (-1)` → `&pixlblink_off POS`.
See `PATCH_PIXLBLINK_KEYMAP_SNIPPET_v002.md`.

## Verification (inspection only, not a Zephyr build)
Brace/paren balance OK; standalone `-Wall -Wextra` test of the slot model and the
`PB_INST` config macro: two presets at 0.5 Hz and 1 Hz blink at independent rates,
colors scale correctly, clear works and leaves other slots active. First real test
is a fork CI build + flash.
