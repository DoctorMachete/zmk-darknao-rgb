# PATCH — hold-tap `require-prior-idle-ms` fires while "disabled"

**Status: implemented in the fork, not yet built or flashed.** A one-condition guard in
`is_quick_tap()` so prior-idle only applies when it is `> 0`. Upstream ZMK `main` carries
the same code; the fork inherited it unchanged.

Shared background: `PATCH_ARCHITECTURE.md`.

---

## 1. Symptom

ACCENTS layer on. The accent keys use `HTUI_1tap_1tap`: hold = accented letter,
tap = plain letter, both through `&macroUI_1tap`, `tap-preferred`, no quick-tap or
prior-idle properties.

| sequence | expected | before fix |
|---|---|---|
| `n`↓ `i`↓ `n`↑, keep holding `i` | `ní` | `ni` |
| `i`↓ `e`↓ `i`↑, keep holding `e` | `ié` | `ie` |
| hold `n` (→ `ñ`), press `i` before 250 ms, keep holding `i` | `ñí` | `ñi` |
| `m`↓ `i`↓ `m`↑ (`m` is plain `&kp`) | `mí` | `mí` ✔ |

## 2. Root cause

A `require-prior-idle-ms` of `-1` (the default, documented as "disabled") only stays
inert while `last_tapped` lies in the past. The check is

```c
if ((last_tapped.timestamp + require_prior_idle_ms) > hold_tap->timestamp) return true;
```

and it silently assumes `last_tapped` can never be later than this hold-tap's press.
Three facts break that assumption:

1. **Capture and replay.** While hold-tap A is undecided, other presses are captured. On
   decision, A's binding is pressed **first**, then the captured presses are replayed
   with their **original** timestamps (`decide_hold_tap`: `press_binding()` then
   `release_captured_events()`).
2. **Macros stamp "now".** A macro emits through `zmk_behavior_queue_add()`, and
   `behavior_queue.c` invokes each item with `.timestamp = k_uptime_get()`, i.e. the
   decision moment. With wait 0 it drains inline, before the replay.
3. **The keycode listener keeps the newest stamp.** `store_last_tapped()` advances
   `last_tapped` to that decision-time stamp.

Timeline with `n` at 0 ms, `i` at 60 ms, `n` released at 110 ms: `n` taps, its macro
stamps `N` at **110**, then `i`'s press is replayed at **60**: `110 + (-1) > 60` → true →
instant tap. The hold side is identical: the tapping-term timer fires at 250, the hold
macro stamps `ñ` at 250, and any hold-tap captured before 250 taps instantly.

`&kp` doesn't trigger it, because `&kp` emits with the hold-tap's own press time.

## 3. The fix — `app/src/behaviors/behavior_hold_tap.c`

```c
static bool is_quick_tap(struct active_hold_tap *hold_tap) {
    if (hold_tap->config->require_prior_idle_ms > 0 &&
        (last_tapped.timestamp + hold_tap->config->require_prior_idle_ms) > hold_tap->timestamp) {
        return true;
    } else { /* same-key quick-tap-ms branch, unchanged */ }
}
```

### Why `> 0`

With `-1` the old branch fires only if `last ≥ press + 2`; with `0`, only if
`last > press`. Either way it can **only** fire in the future-stamp case, so disabling
both removes the bug and nothing else. That includes the keymap's four hold-taps that set
`require-prior-idle-ms = <0>` (`HTUI_1tap_kp_tpref`, `HTUI_1tap_kp_tpref_350`,
`HTUI_1tap_slshort_tpref`, `HTUI_1tap_KCANCELsl_tpref`), which were exposed to the same
bug.

### Alternatives rejected

- **Stamp queued items with the original press time** (`behavior_queue.c`). This would
  break hold-taps started from inside macros, which the keymap uses deliberately
  (`macro_press` → hold-tap → `macro_pause_for_release`, for autoshift):
  `on_hold_tap_binding_pressed()` computes the remaining tapping term from that
  timestamp, so backdating it shortens those terms. Items with a real wait are also
  genuinely emitted later, so "now" is the honest stamp for them.
- **Ignore any `last_tapped` stamp later than the press** (for every prior-idle value,
  not just ≤ 0). It also fixes the positive-value case in §4, but it regresses exactly
  what positive prior-idle exists to protect. In a fast roll — accent tapped via macro
  (stamp 70), then `V` (prior-idle 125) captured at 40 — the true previous key was only
  40 ms earlier, but the stamp is discarded, `V` is treated as idle and can resolve to
  Ctrl. Confirmed in the simulation (§5).

## 4. Constraints and known limits

- **Positive prior-idle values are untouched**, by design: 50, 100, 125, 150, 200, 250,
  300, 400, 500, 600 all appear in the keymap.
- **Non-goal:** a hold-tap with a *positive* prior-idle can still be pushed into a tap by
  a late macro stamp (accent held ~200 ms, `V` pressed at 190, accent released at 220 →
  `V` taps). Fixing that needs the true press time of the previous key, which
  `last_tapped` no longer has once the macro stamp overwrites it.
- **Related pre-existing effect, not fixed here:** the same macro stamp also breaks the
  same-key `quick-tap-ms` branch. `store_last_hold_tapped()` records
  `{position, press time}`, but the macro's later keycode then overwrites `last_tapped`
  with `{INT32_MIN, decision time}`, so the next press of the same key no longer matches
  on position. Predicted effect: double-tap-then-hold doesn't force a tap on hold-taps
  whose **tap binding emits a keycode through a macro**. Of the keymap's 20 hold-taps
  with `quick-tap-ms`, the candidates are `HTUI_smart_spaces_afteri_bal` and
  `HTUI_smart_num_space_REPEATbal` (and any mod-morph whose children are macros). Unchanged by
  this patch — pinned and patched agree. Unverified on hardware.
- No split, RGB or payload involvement.
- **Deploy ritual:** fork source change ⇒ rebuild, **bump the fork SHA in `west.yml`**,
  reflash both halves.

## 5. Verification done off-hardware

- `last_tapped`, `store_last_tapped()`, `store_last_hold_tapped()` and `is_quick_tap()`
  extracted **verbatim** from both the unpatched and patched file and driven by a host
  harness (`gcc -Wall -Wextra`, clean):

  | scenario | before | after |
  |---|---|---|
  | `n` tap via macro → `i` replayed, prior-idle −1 | taps now | can hold |
  | `n` hold via macro at 250 → `i` at 120 | taps now | can hold |
  | `e` hold via macro at 250 → `i` at 200 | taps now | can hold |
  | macro tap → hold-tap with prior-idle 0 | taps now | can hold |
  | `&kp` tap → `i` (control) | can hold | can hold |
  | prior-idle 125, previous key 60 ms earlier | taps now | taps now |
  | prior-idle 125, previous key 200 ms earlier | can hold | can hold |
  | fast roll: accent macro at 70, `V` (125) at 40 | taps now | taps now |
  | quick-tap 200, same key again at 150 | taps now | taps now |
  | quick-tap 200, same key again at 300 | can hold | can hold |

- `git apply --check` passes against the fork as uploaded.
- ZMK's own hold-tap tests only use `require-prior-idle-ms` of 100 and 500, so none
  takes the changed path.
- **Not compiled into firmware, not tested on hardware.**

## 6. Hardware test (keymap unchanged, ACCENTS on)

- `n`↓ `i`↓ `n`↑, hold `i` → `ní`
- `i`↓ `e`↓ `i`↑, hold `e` → `ié`
- hold `n` until `ñ`, with `i` pressed before `ñ` appears, then hold `i` → `ñí`
- **Regressions:** fast typing ("niño", "compañía", "piénsalo") gives no stray accents;
  `V`/`K` don't turn into Ctrl when typed fast; space still taps when typed fast;
  double-tap-then-hold on an `&kp`-tap key with `quick-tap-ms` still taps. (Don't count
  the smart-space keys here; see §4.)

## 7. Files

**Modified (1):** `app/src/behaviors/behavior_hold_tap.c`.
**New (1):** this doc. **Deleted:** none. Keymap unchanged.
