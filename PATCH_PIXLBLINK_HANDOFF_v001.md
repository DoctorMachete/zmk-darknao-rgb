# PIXLBLINK — Feature Handoff

> **ARTIFACT VERSIONS (this set):**
> - `PATCH_PIXLBLINK_HANDOFF_v001.md` (this file)
> - `PATCH_PIXLBLINK_DESIGN_v001.md`
> - `PATCH_PIXLBLINK_KEYMAP_SNIPPET_v001.md`
> - `PATCH_PIXLBLINK-test-howto.md`
>
> Naming convention: docs describing changes to the darknao fork carry a `PATCH_`
> prefix + feature token + doc type + zero-padded `vNNN`; each family versioned
> independently; bump on regeneration and update this block. Functional config
> files (`west.yml`, `main.yml`) are NOT prefixed/versioned. Feature token is
> `PIXLBLINK` (no `E`) so searches don't collide with `PIXEL`/`&pixel`.

---

## PART -1 — Document lineage (READ FIRST)
- This set describes a **new** behavior to add on top of the already-patched
  darknao fork (`zmk-darknao-rgb-rgb-layer-24.12C`, the tree that already contains
  `&pixel` + the MAGIC indicator layer).
- **Status of PIXLBLINK: DESIGN / SPEC ONLY.** No code in the fork yet. This
  handoff + the DESIGN doc are the implementation contract for the build session.
- Sibling family already in the tree and CONFIRMED WORKING on hardware: `&pixel`
  (see `PATCH_PIXEL_*`). PIXLBLINK reuses its addressing and split model.

## PART 0 — Repo / build topology (applies to everything)
- Fork: darknao/zmk, branch `rgb-layer-24.12` (patched `…24.12C` tree here).
- `rgb_underglow.c` compiles on **both** halves (CMake gate = `CONFIG_ZMK_RGB_UNDERGLOW`),
  so the peripheral runs its own 25 ms `underglow_tick` and its own composite —
  **it can animate a blink locally** from a spec pushed once over the split.
- Any change here is a **fork source change**: rebuild the fork and **bump the SHA
  pin in `west.yml`** in the keymap repo (a warm keymap-only rebuild will NOT pick
  it up — the fork is SHA-pinned, not branch-tracked).
- Both halves must be reflashed after any firmware change touching either side.

---

## PART A — What PIXLBLINK is
A new behavior `&pixlblink` that blinks one pixel **sharply** (hard on/off) between
an author color and **black**, on either half, at a **fixed hardcoded rate**
(v001 = 0.5 Hz). `&pixel` is untouched. Full technical detail:
`PATCH_PIXLBLINK_DESIGN_v001.md`.

### A1. v001 simplifications (by request)
- One color; off-phase is implicit black.
- Square wave only (no fade/breathe).
- Frequency hardcoded in C, `code = 5` → 0.5 Hz (encoding **Hz = code/10**;
  `10` → 1 Hz). Not a binding param yet; may move to `go60.conf` later.
- Two binding params: `param1 = position`, `param2 = color` (negative clears).
- Same GLOBAL address space and split routing as `&pixel`.

### A2. Parameters / API
`compatible = "zmk,behavior-pixlblink"`, `#binding-cells = <2>`,
`.locality = BEHAVIOR_LOCALITY_GLOBAL`.
- `param1` GLOBAL position: `0..local_count-1` LEFT, `>=local_count` RIGHT
  (relayed as `pos - local_count`).
- `param2` color `0xRRGGBB`; negative → stop/clear the blink at that position.

### A3. Architecture (one line)
A third parallel layer — `pixlblink_color[]` / `pixlblink_active[]` /
`any_pixlblink` — composited above `&pixel`, evaluated each 25 ms tick to a
square wave whose phase is read statelessly from `k_uptime_get()`. See DESIGN for
the buffer/gate/tick specifics.

---

## PART B — Files to change (implementation checklist)
**New**
- [ ] `app/src/behaviors/behavior_pixlblink.c` (clone of `behavior_pixel_color.c`;
      route local via `zmk_rgb_underglow_set_pixlblink`, remote via new split calls)
- [ ] `app/dts/behaviors/pixlblink.dtsi` (declare `&pixlblink` node)
- [ ] `app/dts/bindings/behaviors/zmk,behavior-pixlblink.yaml` (`two_param`)

**Modified**
- [ ] `app/src/rgb_underglow.c` — buffers + `zmk_rgb_underglow_set_pixlblink()` +
      per-tick square-wave in the composite + **`any_pixlblink` in the ext-power
      gate** + keep tick alive while `any_pixlblink`
- [ ] `app/include/zmk/rgb_underglow.h` — declare setter (+ `PIXLBLINK_FREQ_CODE`)
- [ ] `app/include/zmk/split/transport/types.h` — ops `PIXLBLINK`=7,
      `PIXLBLINK_CLEAR`=8
- [ ] `app/src/split/…central.c` + `app/include/zmk/split/central.h` — senders
- [ ] `app/src/split/bluetooth/service.c` — handle the two new ops
- [ ] `app/CMakeLists.txt` — register `behavior_pixlblink.c` under
      `CONFIG_ZMK_RGB_UNDERGLOW`
- [ ] `app/dts/behaviors.dtsi` — `#include <behaviors/pixlblink.dtsi>`

**Optional / future**
- [ ] `go60.conf` — `CONFIG_ZMK_PIXLBLINK_FREQ_CODE` when rate leaves C
- [ ] keymap — declare the `&pixlblink` node + use it in a listener
      (`PATCH_PIXLBLINK_KEYMAP_SNIPPET_v001.md`)

---

## PART C — Verification plan
- Inspection: brace balance + `gcc -fsyntax-only` on the new/edited C, exactly as
  the MAGIC layer was checked.
- First real test is a fork CI build + flash (see `PATCH_PIXLBLINK-test-howto.md`).
- Bench test: one LEFT pixel and one RIGHT pixel blinking; confirm both toggle at
  ~0.5 Hz and that clearing with `(-1)` reveals the pixel/underglow beneath.

## PART D — Most likely first-build/flash failures (in order)
1. `any_pixlblink` missing from the ext-power gate → invisible when RGB off (the
   MAGIC-layer bug — do not repeat).
2. Tick stopped while RGB off → first phase then freeze.
3. New split ops not handled in the peripheral `switch` → remote blink ignored.
4. `behavior_pixlblink.c` not in CMake or `.dtsi` not included → `&pixlblink`
   undefined at DT parse → whole keymap fails to build.
5. `#binding-cells` ≠ `<2>`.

## PART E — Open / pending items
- [ ] Decide PIXLBLINK vs MAGIC priority at a shared position (default: MAGIC wins).
- [ ] Decide whether group-clear sentinels apply to PIXLBLINK or it gets its own.
- [ ] Decide L/R phase-sync requirement (v001: accept drift).
- [ ] Confirm final frequency encoding before promoting to `.conf`/param.
