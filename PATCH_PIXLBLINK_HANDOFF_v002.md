# PIXLBLINK — Feature Handoff (v002, preset model)

> **ARTIFACT VERSIONS:** DESIGN/HANDOFF/KEYMAP_SNIPPET/IMPL_MANIFEST at v002,
> test-howto shared. v002 supersedes v001. Feature token `PIXLBLINK` (no `E`).

## PART -1 — Lineage
- Adds two behaviors on top of `zmk-darknao-rgb-rgb-layer-24.12C` (the tree with
  `&pixel` + MAGIC + PIXLBLINK v001).
- **v002 replaces v001's `&pixlblink`** (Option A migration): the 2-cell
  color↔black form is gone; presets + a fixed off behavior take its place.
- Status: IMPLEMENTED in the fork (this overlay), inspection-verified only — first
  CI build/flash is the real test. `&pixel` remains the confirmed-working sibling.

## PART 0 — Build topology
- `rgb_underglow.c` compiles on both halves; peripheral animates locally from a
  spec pushed once over the split.
- Fork source change => rebuild fork + bump `west.yml` SHA. Reflash both halves.

## PART A — The two behaviors
1. **`zmk,behavior-pixlblink` (preset, user-defined, multi-instance)** —
   `#binding-cells = <1>` (position). DT props `color1`/`color2`/`frequency`
   (defaults white/black/5). Blinks color1<->color2 at freq_hz = frequency/10.
   Config read per-instance via `dev->config`. GLOBAL locality.
2. **`zmk,behavior-pixlblink-off` (fixed, NOT user-configurable)** —
   `#binding-cells = <1>` (position). One shipped instance `&pixlblink_off`.
   Stops the blink at a position. GLOBAL locality.

## PART B — Files (see IMPL_MANIFEST for the full checklist)
New: behavior_pixlblink_off.c, zmk,behavior-pixlblink-off.yaml.
Rewritten: behavior_pixlblink.c, pixlblink.dtsi, zmk,behavior-pixlblink.yaml.
Modified: rgb_underglow.c (+.h), types.h, central.h, central.c, service.c, CMakeLists.txt.

## PART C — Verification
Brace/paren balance OK; standalone logic test of the two-color per-frequency slot
model + the PB_INST config macro pass (independent rates confirmed, colors scale,
clear works). Not a Zephyr build.

## PART D — First-build/flash watch items
1. ext-power gate includes any_pixlblink (done) — else invisible when RGB off.
2. refresh timer stays alive while blinking (done) — else freezes after one phase.
3. peripheral rgb_pixel_msg is __packed & matches wire (done) — else wire corruption.
4. peripheral handles both PIXLBLINK ops with color2+freq_code (done).
5. ≥1 preset instance exists so the preset driver compiles in; &pixlblink_off node
   shipped so the off driver compiles in (done).

## PART E — Open items
- Confirm PIXLBLINK vs MAGIC priority at a shared position (currently MAGIC wins).
- Consider moving PIXLBLINK_DEFAULT_FREQ_CODE to go60.conf if a global default is wanted.
- Keymap migration from v001 usages (breaking) — see KEYMAP_SNIPPET.
