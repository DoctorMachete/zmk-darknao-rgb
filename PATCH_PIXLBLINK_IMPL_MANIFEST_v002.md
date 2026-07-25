# PIXLBLINK — fork implementation manifest (v002, preset model)

Implements the preset `&pixlblink` (named instances, two colors + per-instance
frequency, 1 cell = position) and the fixed `&pixlblink_off` on top of
`zmk-darknao-rgb-rgb-layer-24.12C`. **v002 replaces v001's `&pixlblink`** (the
2-cell color↔black form is removed — Option A).

## How to apply
Extract the overlay at the ROOT of the fork (docs land at root next to the
`PATCH_PIXEL_*` set; `app/...` paths overwrite/add). Rebuild and bump `west.yml` SHA.

## New files (2)
- `app/src/behaviors/behavior_pixlblink_off.c` — fixed single-instance off
  behavior; param1 = position; local→`clear_pixlblink`, remote→`clear_pixlblink`
  over the split. GLOBAL locality.
- `app/dts/bindings/behaviors/zmk,behavior-pixlblink-off.yaml` — `one_param`.

## Rewritten files (3)
- `app/src/behaviors/behavior_pixlblink.c` — now multi-instance preset:
  `behavior_pixlblink_config {color1,color2,frequency}` built per instance via
  `DT_INST_PROP`, instantiated with `DT_INST_FOREACH_STATUS_OKAY(PB_INST)`. Handler
  reads the invoked instance's config through
  `zmk_behavior_get_binding(binding->behavior_dev)->config`. param1 = position.
- `app/dts/behaviors/pixlblink.dtsi` — ships the fixed `&pixlblink_off` node;
  documents the preset node shape (presets themselves live in the keymap).
- `app/dts/bindings/behaviors/zmk,behavior-pixlblink.yaml` — `one_param` +
  properties `color1` (default 0xFFFFFF), `color2` (default 0x000000),
  `frequency` (default 5).

## Modified files (7)
1. `app/include/zmk/rgb_underglow.h` — new signatures
   `set_pixlblink(pos, color1, color2, freq_code)` and `clear_pixlblink(pos)`;
   `PIXLBLINK_DEFAULT_FREQ_CODE` (5) replaces the old fixed `PIXLBLINK_FREQ_CODE`.
2. `app/src/rgb_underglow.c` —
   - `struct pixlblink_slot {color1,color2,half_ms,active}` array replaces the
     single-color buffers; `pixlblink_half_ms(code)` helper (5000/code, guarded).
   - composite stamps color1/color2 per slot using each slot's OWN half_ms
     (independent per-pixel rates); overlay mask set on both phases.
   - two-color setter + dedicated `clear_pixlblink()`; `pixlblink_recompute_any()`
     and `pixlblink_scale()` helpers.
   - carried over from v001 (still present): `any_pixlblink` in the ext-power gate;
     refresh timer kept alive while `any_pixlblink`.
3. `app/include/zmk/split/transport/types.h` — `set_rgb_pixel` gains
   `uint32_t color2` + `uint8_t freq_code` (ops 7/8 unchanged; `color` reused as color1).
4. `app/include/zmk/split/central.h` — sender signature
   `set_pixlblink(pos, color1, color2, freq_code)`.
5. `app/src/split/central.c` — sender populates color2 + freq_code.
6. `app/src/split/bluetooth/service.c` — local `rgb_pixel_msg` gains color2 +
   freq_code AND is now `__packed` to match the packed wire struct byte-for-byte
   (bulk memcpy); handler passes the new fields / calls `clear_pixlblink`.
   NOTE: the `__packed` also fixes a latent v001 field-offset mismatch on `color`.
7. `app/CMakeLists.txt` — registers `behavior_pixlblink_off.c`
   (behavior_pixlblink.c already registered).

*(`app/dts/behaviors.dtsi` already includes `pixlblink.dtsi` from v001 — both
compatibles resolve through it; no change needed.)*

## Verification (inspection only — NOT a Zephyr build)
- Brace/paren balance OK on all edited C files; no stray non-ASCII (only em-dashes
  in comments, matching existing style).
- Standalone `-Wall -Wextra` test of the slot model: preset A (orange↔red @0.5 Hz,
  half=1000) and preset B (white↔black @1 Hz, half=500) blink at independent rates;
  colors scale to BRT_MAX correctly; `clear` deactivates one slot while the other
  keeps `any_pixlblink` true.
- Standalone test of the `PB_INST` config-struct macro expansion + `dev->config`
  access: compiles clean, right values.
- Split struct layout verified: packed wire vs packed local `rgb_pixel_msg` match
  byte-for-byte (size 20, identical field offsets).

## Migration (breaking, one-time)
- `&pixlblink POS COLOR` → define a preset and use `&<preset> POS`.
- `&pixlblink POS (-1)` → `&pixlblink_off POS`.

## Keymap
Not included (fork only). See `PATCH_PIXLBLINK_KEYMAP_SNIPPET_v002.md`.
