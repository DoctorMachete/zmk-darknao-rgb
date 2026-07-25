# PIXLBLINK — sharp single-color blink behavior (`&pixlblink`)

> **ARTIFACT VERSIONS (this set):**
> - `PATCH_PIXLBLINK_DESIGN_v001.md` (this file)
> - `PATCH_PIXLBLINK_HANDOFF_v001.md`
> - `PATCH_PIXLBLINK_KEYMAP_SNIPPET_v001.md`
> - `PATCH_PIXLBLINK-test-howto.md`
>
> **Naming convention (matches the PIXEL family).** Docs that describe changes to
> the darknao fork (original or already-patched code) carry a `PATCH_` prefix, the
> feature token (`PIXLBLINK`), the doc type, and a zero-padded `vNNN`. Each family
> is versioned independently; bump on every regeneration and update this block.
> Functional config files (`west.yml`, `main.yml`) are NEVER prefixed/versioned —
> they keep their real names so the build can find them.
>
> **Feature token is deliberately `PIXLBLINK` (no `E`)** so future searches for this
> behavior do not collide with `PIXEL` / `&pixel` results.

---

## Goal
Add a new behavior `&pixlblink` that makes a single pixel blink **sharply**
(hard on/off square wave) between an author-chosen color and **black (off)**, on
**either half**. It reuses the `&pixel` addressing and split model so it feels
like a sibling of the original behavior.

The existing `&pixel` behavior is **unchanged** — static per-pixel overrides keep
working exactly as they do now. PIXLBLINK is a separate, parallel layer.

---

## Scope of THIS version (v001) — intentionally minimal
Simplifications agreed for the first cut, to keep it close to `&pixel`:

1. **One color, alternating with black.** The "off" phase is always `0x000000`.
   No second author color. (A future version could generalize the off-color.)
2. **Sharp pulsation, not smooth.** Pure square wave: full-on for a half-period,
   full-off for a half-period. No fade / breathe interpolation.
3. **Frequency is fixed and hardcoded in C** for now, starting at **0.5 Hz**.
   - Encoding (baked into the C constant): **Hz = code / 10**.
     So `code = 5` → 0.5 Hz, `code = 10` → 1 Hz.
   - v001 hardcodes `PIXLBLINK_FREQ_CODE = 5` (0.5 Hz). **Not** a binding param yet.
   - *Future:* move the constant to `go60.conf` (e.g. `CONFIG_ZMK_PIXLBLINK_FREQ_CODE`),
     or promote it to a third source (DT node) if per-blinker rates are ever wanted.
4. **Two binding parameters**, same shape as `&pixel`:
   - `param1` = **GLOBAL position** (same address space as `&pixel`).
   - `param2` = **color** (`0xRRGGBB`); a negative value **stops** the blink
     (clears it — see "Clear semantics").

> **Why this still fits two params:** color is a single value (the off-color is
> implicit black), and frequency is not a param in v001 (hardcoded). So
> `position + color` is enough, mirroring `&pixel`.

---

## Frequency / timing math (square wave)
- The underglow render tick runs at **25 ms** on each half (`underglow_tick`,
  `K_MSEC(25)` in `rgb_underglow.c`). This is the animation clock.
- `freq_hz = code / 10`. Full period `T = 1 / freq_hz` seconds.
  Half-period (on, then off) `= T / 2`.
- v001 default `code = 5` → `freq = 0.5 Hz` → `T = 2000 ms` → **1000 ms on,
  1000 ms off**. At 25 ms/tick that is **40 ticks per half-period**.
- Phase is derived from a free-running millisecond clock so no per-tick counter
  state is needed:
  ```c
  // half_ms = 500 / freq_hz = 5000 / code   (code=5 -> 1000 ms)
  bool on_phase = ((k_uptime_get() / half_ms) & 1) == 0;
  ```
- **Cross-half phase note.** Left and right run independent clocks and will drift
  relative to each other over time. For a slow status blink this is usually
  invisible; if strict L/R sync is ever required, add a phase-epoch reset pushed
  over the split (out of scope for v001).

---

## Architecture — a third parallel layer (mirrors the MAGIC layer pattern)
The fork already composites, in priority order:
`pixels[]` (underglow/layer RGB) → `pixel_overrides[]` (`&pixel`) →
`magic_pixels[]` (indicators). PIXLBLINK adds one more parallel registry+buffer,
so the existing composite path renders it with no special-casing at the blend
stage.

New central-side (and peripheral-side) state in `rgb_underglow.c`:
```c
// Sharp single-color blink layer (&pixlblink). Alternates color <-> black.
static struct led_rgb pixlblink_color[STRIP_NUM_PIXELS]; // the "on" color
static bool          pixlblink_active[STRIP_NUM_PIXELS];  // is this pos blinking?
static bool          any_pixlblink;                       // fast-path gate
```
- **Setter:** `zmk_rgb_underglow_set_pixlblink(pos, color)` — `color < 0` clears
  that position (stops blinking, reveals whatever is beneath); otherwise records
  the on-color and sets `pixlblink_active[pos] = true`.
- **Per-tick evaluation:** in `zmk_rgb_underglow_tick()` (or inside the composite
  in `generate_status()` / `zmk_led_write_pixels()`), for each active position
  write either the stored color (on phase) or `{0,0,0}` (off phase) into the
  composite, ABOVE the `&pixel` layer. Because the phase comes from `k_uptime_get()`,
  the tick just reads the clock — it does not accumulate counters.
- **Priority:** place PIXLBLINK relative to MAGIC deliberately. Suggested order
  low→high: underglow → `&pixel` → **PIXLBLINK** → MAGIC (so a live BLE/USB
  indicator still wins over a decorative blink). Document whichever is chosen.

> **CRITICAL — power gate.** The single most important integration point (this is
> exactly the bug that bit the MAGIC layer): the ext-power enable gate in
> `zmk_rgb_set_ext_power()` must include `any_pixlblink`, or a blink started while
> RGB underglow is OFF will be written to an unpowered strip and show nothing:
> ```c
> int desired_state = state.on || state.status_active || any_pixel_override ||
>                     any_magic_pixel || any_pixlblink || any_persist_indicator();
> ```
> Also make sure the render tick actually keeps running while a blink is active
> even if `state.on` is false — the blink needs the 25 ms timer alive to animate.
> (If the timer is stopped in `underglow_off`, PIXLBLINK must keep/restart it while
> `any_pixlblink` is true.)

---

## Clear semantics (same mental model as `&pixel`)
- `&pixlblink POS (-1)` → **stop** blinking at POS and clear it (reveals the
  `&pixel` override / underglow beneath). This is the analogue of `&pixel POS (-1)`.
- `&pixlblink POS 0x000000` → a blink whose "on" color is black. This is a no-op
  visually (black↔black) and is almost certainly NOT what you want — use `(-1)` to
  actually stop it. Mirrors the `&pixel` "black vs. cleared" distinction.
- Consider honoring the existing group-clear sentinels
  (`ZMK_PIXEL_CLEAR_LEFT/RIGHT/ALL`) for PIXLBLINK too, or add dedicated
  `PIXLBLINK_CLEAR_*` — decide and document. v001 minimum: per-position `(-1)`.

---

## Split model — reuse the `&pixel` channel, add ops
The peripheral runs its own `rgb_underglow.c` and its own 25 ms tick, so it can
**animate autonomously** — the central only needs to push the *spec* once, not a
frame stream. That is the same realization the MAGIC feature relied on.

- **New ops** in `enum zmk_split_rgb_pixel_op` (`app/include/zmk/split/transport/types.h`):
  ```c
  ZMK_SPLIT_RGB_PIXEL_OP_PIXLBLINK       = 7, // start blink: position, color
  ZMK_SPLIT_RGB_PIXEL_OP_PIXLBLINK_CLEAR = 8, // stop blink at position
  ```
  (v001 needs no new payload fields — `position` + existing `color` suffice, since
  frequency is hardcoded identically on both halves. If frequency later becomes
  per-blink, add a `freq_code` byte to the `set_rgb_pixel` struct.)
- **Central senders** (`app/src/split/…central.c` + `zmk/split/central.h`):
  `zmk_split_central_set_pixlblink(pos, color)` /
  `zmk_split_central_clear_pixlblink(pos)`, mirroring the existing
  `..._set_pixel` / `..._clear_pixel`.
- **Peripheral receiver** (`app/src/split/bluetooth/service.c`, the `switch(op)`
  near the `OP_SET` case): call `zmk_rgb_underglow_set_pixlblink(msg.position,
  (int32_t)msg.color)` for start, and `(… , -1)` for clear.

---

## Behavior parameters / API (v001)
| param  | meaning                                                              |
|--------|---------------------------------------------------------------------|
| param1 | GLOBAL position. `0 .. local_count-1` = LEFT; `>= local_count` = RIGHT (relayed as `pos - local_count`). Same as `&pixel`. |
| param2 | Color `0xRRGGBB`. Negative → stop/clear the blink at this position.  |

`#binding-cells = <2>`, `compatible = "zmk,behavior-pixlblink"`,
`.locality = BEHAVIOR_LOCALITY_GLOBAL` (identical to `&pixel`).

---

## Files touched (planned — for the HANDOFF checklist)
**New:**
- `app/src/behaviors/behavior_pixlblink.c` — behavior (clone of
  `behavior_pixel_color.c`, routing set/clear + remote via new split calls).
- `app/dts/behaviors/pixlblink.dtsi` — declares the `&pixlblink` node.
- `app/dts/bindings/behaviors/zmk,behavior-pixlblink.yaml` — `two_param`.

**Modified:**
- `app/src/rgb_underglow.c` — `pixlblink_*` buffers, setter, per-tick square-wave
  evaluation in the composite, **`any_pixlblink` added to the ext-power gate**, and
  keep-tick-alive-while-blinking.
- `app/include/zmk/rgb_underglow.h` — declare `zmk_rgb_underglow_set_pixlblink()`
  (+ the hardcoded `PIXLBLINK_FREQ_CODE` / helper if placed here).
- `app/include/zmk/split/transport/types.h` — two new ops.
- `app/src/split/…central.c` + `app/include/zmk/split/central.h` — new senders.
- `app/src/split/bluetooth/service.c` — handle the two new ops on the peripheral.
- `app/CMakeLists.txt` — register `behavior_pixlblink.c` under
  `CONFIG_ZMK_RGB_UNDERGLOW`.
- `app/dts/behaviors.dtsi` — `#include <behaviors/pixlblink.dtsi>`.

*(Optional, future)* `go60.conf` — `CONFIG_ZMK_PIXLBLINK_FREQ_CODE` once the rate
moves out of C.

---

## Design decisions made (overridable)
- Off-color hardcoded to black (implicit) — keeps it to one param.
- Frequency hardcoded (`code=5`, 0.5 Hz) — no param, no `.conf` yet, by request.
- Phase from `k_uptime_get()` (stateless) rather than a per-position counter.
- PIXLBLINK composited BELOW MAGIC (indicators win) — flip if decorative blink
  should override indicators.
- No new split payload fields in v001 (frequency identical both sides).

## Most likely first-build / first-flash failure points (in order)
1. **Forgot `any_pixlblink` in the ext-power gate** → blinks invisibly when RGB is
   off. (Exact MAGIC-layer bug.)
2. **Tick stopped while RGB off** → first phase paints, then freezes (no animation).
3. New split ops added to the enum but not handled in the peripheral `switch` →
   remote blink silently ignored.
4. `behavior_pixlblink.c` not registered in CMake, or `pixlblink.dtsi` not
   included → `&pixlblink` unknown at DT parse → **whole keymap fails to build**
   (same class as the earlier undeclared-node issue).
5. `#binding-cells` mismatch (must be `<2>`).
