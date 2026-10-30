# PATCH — mirror mode (`zmk,behavior-mirror`) + host notifier (`0xF4`)

**Status: implemented and tested off-hardware, not yet flashed.** 17 native_posix cases,
build-failure checks, a host test of the packet bytes, the full suite, and GO60 builds
with and without zmk-raw-hid (§9).

A mirror mode with **no layer behind it**. While an instance is on, chosen keys on chosen
layers act as their mirror key, until it's switched off. It replaces hand-kept mirror
layers (`LAYER__MIRROR`) and costs zero layers per mirrored layer. A notifier tells the
host overlay (LayerOverlay_PEEK) which mirror is on, over Raw HID.

Spec: the side-chat handoff "Mirror mode (`&mirror`) for the GO60 fork" (2026-10-30), with
the deviations in §8. Packet: `MIRROR_PACKET_0xF4.md` (project doc). Background:
`PATCH_ARCHITECTURE.md` §7.1 (listener order).

---

## 1. Keymap

```dts
#include <dt-bindings/zmk/mirror.h>

/ { behaviors {
    mirror_full: mirror_full {
        compatible = "zmk,behavior-mirror";
        #binding-cells = <1>;
        overlay-id = <1>;                                  // for the host overlay (§6)
        mirror-positions = <MIRROR_MAP_GO60_FROW_SWAP>;    // optional on GO60
        not-press-release-outputs;                         // for &pixel / &pixlblink feedback
        enter = <&pixel PIXEL_LH_C6R4 PX_BLUE>;
        exit  = <&pixel PIXEL_LH_C6R4 (-1)>;

        alphas {
            layers = <LAYER__CMKDH LAYER__ANDROID LAYER__SMSHFT_ON /* … every layer that can sit on top while typing */>;
            excluded-positions = <POS_LH_T1 POS_LH_T2 POS_LH_T3 POS_RH_T3 POS_RH_T2 POS_RH_T1>;
        };
        accents {
            layers = <LAYER__ACCENTS>;
            excluded-positions = <POS_LH_T1 POS_LH_T2 POS_LH_T3 POS_RH_T3 POS_RH_T2 POS_RH_T1 POS_LH_C1R2>;
        };
    };
}; };

/* bindings */
&mirror_full MIRROR_TOG
```

### 1.1 The parameter

| value | does |
|---|---|
| `MIRROR_TOG` | on (switching off any other instance) / off |
| `MIRROR_ON` | on. If it's already on as a one-shot or momentary, it becomes permanent |
| `MIRROR_OFF` | off, if this instance is the one on; otherwise nothing |
| `MIRROR_MO` | on while held, off on release. Nothing if this instance is already on |
| `MIRROR_ONESHOT` | on until the release of the next key that actually gets mirrored. Excluded keys, keys that map to themselves and unlisted layers don't use it up. Nothing if already on |

### 1.2 Properties

| property | where | default | meaning |
|---|---|---|---|
| `mirror-positions` | instance | GO60: `MIRROR_MAP_GO60`; elsewhere required | one entry per key position: entry *p* = the key that pressing *p* acts as. A key pointing at itself isn't mirrored. `POS_*` names work |
| `overlay-id` | instance | 0 | id sent to the host while this instance is on (1..255). 0 = not reported: the host sees "no mirror". Give each instance its own |
| `enter` / `exit` | instance | none | behavior chains when this instance turns on / off (also when switched away) |
| `not-press-release-outputs`, `tap-ms`, `wait-ms` | instance | off, 5, 5 | as on caps word and the idle lock: set the first for pixel feedback (constraint #9) |
| `layers` | each child | required | layers this group mirrors on |
| `excluded-positions` | each child | none | keys never mirrored while one of this group's layers is on top |

Map macros in `dt-bindings/zmk/mirror.h`:
- `MIRROR_MAP_GO60` reflects each row (row 1: `11 10 … 0`), the row-5 keys and the thumbs.
- `MIRROR_MAP_GO60_FROW_SWAP` is the same, but row 1 swaps halves in order (`6 7 8 9 10 11
  0 1 2 3 4 5`), so F1 stays leftmost.

GO60 key numbers: rows of 12 for rows 1–4 (0–47), row 5 = 48–53 (`LH_C4R5 LH_C3R5 LH_C2R5
RH_C2R5 RH_C3R5 RH_C4R5`), thumbs = 54–59 (`LH_T1 LH_T2 LH_T3 RH_T3 RH_T2 RH_T1`).

## 2. Rules

1. **Decided per key press.** A press is mirrored when an instance is on, the **highest
   active layer** is in one of its groups, and the key isn't in that group's
   `excluded-positions`.
   - Holding an unlisted layer (NAV, LNPAD, …) turns mirroring off for those presses.
   - Layers that sit on top while typing (SMSHFT_*, SMI_*, ADPT_SQNC, conditional layers)
     must be listed, or mirroring stops while they're on.
   - The highest-active-layer rule is the one combos use too.
2. **Press and release stay paired.** Each physical key remembers what its press was sent
   as; its release goes there, even if the mode changed or switched off in between.
   Nothing gets stuck.
3. **One key, one holder.** If the key a press would produce is already held through
   another physical key (two map entries pointing at one key, or the mirror twin held
   while mirroring is off), the press is ignored, and so is its release.
4. **One instance at a time.** Turning B on while A is on runs A's `exit`, then B's
   `enter`. Turning on an instance that's already on does nothing (except `MIRROR_ON`
   making a one-shot or momentary permanent).
5. **The on/off key must not get mirrored away.** Put it in `excluded-positions` of every
   group whose layers it's reachable from, or on a key that maps to itself, or use a
   mirrored pair (the `combo_combo_MIRROR_L` / `_R` pattern). Otherwise, once on, pressing
   it sends its mirror twin and the mode can't be switched off from that key.
6. **One-shot ends at the release** of the key that used it. Its `exit` chain runs at that
   moment, just before the key's release reaches the host (test 8: the exit's key press
   shows up between the mirrored key's press and release). Indicators don't care.

## 3. How it works

`app/src/mirror_mode.c` is a listener on `zmk_position_state_changed` that **rewrites the
event's position in place** and bubbles it. Everything after it (hold-tap, combos, the
keymap) sees the mirrored key. Its state is small: per physical key, what the press was
sent as (or "ignored"); per position, whether it's held; the active instance and mode.

### 3.1 ⚠️ Link order decides who sees mirrored keys

Listeners run in link order (`PATCH_ARCHITECTURE.md` §7.1). `app/CMakeLists.txt` places the
file by `CONFIG_ZMK_MIRROR_MODE_COMBOS`:

| option | place | effect |
|---|---|---|
| `y` (default) | right after `layer_idle_lock.c`, ahead of hold-tap and combo | combos and hold-tap decisions see mirrored keys. The idle lock still sees physical keys (it only cares about activity) |
| `n` | after `combo.c`, before `keymap.c` | combos keep their physical keys; hold-tap's decision logic (listed before combo) sees physical keys too. A mirrored mod-tap still works (test 6) |

Checked on the LH build (`arm-zephyr-eabi-nm -n zmk.elf | grep zmk_event_sub_.*position`):
`activity`, **`mirror_mode`**, `behavior_hold_tap`, `behavior_caps_word`, `behavior_tap_dance`,
`keymap` (the stock keymap has no idle lock or combos).

### 3.2 ⚠️ Combo re-raises

`combo.c` re-raises some captured events from the **start** of the chain: the 2nd and
later captured presses in `release_pressed_keys()`, and a duplicate key-up in
`position_state_up()`. With combos mirrored, those events already went through the mirror
once. Re-mirroring them would send the twin's twin, or trip rule 3.

Fix: combo calls `zmk_mirror_mode_pass_next(&event)` right before each of those two
`ZMK_EVENT_RAISE`s, and the mirror lets the next position event through untouched. The
raise is synchronous, and nothing ahead of the mirror captures position events, so the
next event the mirror sees is that one. It also checks position, state and timestamp, and
logs a warning if they don't match (never seen in any test). With mirror mode compiled
out, or with combos not mirrored, the call is an empty inline.

This replaces the spec's "remember (position, timestamp, state) and let matches through"
table. That table can't tell a re-raise from a fresh press of the twin key in the same
millisecond, and keys read in one matrix scan share a timestamp.

Hold-tap re-raises with `ZMK_EVENT_RAISE_AT(…, behavior_hold_tap)`, which continues after
hold-tap, so the mirror never sees those twice.

### 3.3 Other notes

- **Central only** (`!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL`). Right-half keys reach the
  central as position events; nothing crosses the split, and the 20-byte rule is
  untouched.
- `zmk_mirror_mode_changed` (`instance`, `overlay_id`, `active`, `oneshot`) is raised on
  every change; switching raises A off, then B on.
- Things that read keymap bindings (Studio, keypeek, the Prospector overlay) still show
  the unmirrored layout; the `0xF4` packet is how the host overlay knows (§6).
- A key that's `&trans` on the mirrored layer falls through at the **mirrored** key, since
  the event itself was rewritten.
- Per-half RGB key-press effects see physical keys.

## 4. Build-time checks

Each fails the build with a `zmk,behavior-mirror: …` message (all seven forced on
purpose, §9):

| check | message (shortened) |
|---|---|
| `mirror-positions` length ≠ key count | must have one entry per key position |
| an entry ≥ key count | a `mirror-positions` entry is not a key position |
| no `mirror-positions` on a non-GO60 board | is required on this board |
| a layer in two groups of one instance | a layer is listed in two groups of the same instance |
| a `layers` entry ≥ layer count | a group's `layers` entry is not a layer of this keymap |
| an `excluded-positions` entry ≥ key count | … is not a key position |
| no child group | needs at least one child group with `layers` |

The same layer in different instances is fine. Two instances sharing a non-zero
`overlay-id` only logs a warning at boot.

## 5. Kconfig

| symbol | default | meaning |
|---|---|---|
| `ZMK_BEHAVIOR_MIRROR` | y when a `zmk,behavior-mirror` node exists (central only) | the feature |
| `ZMK_MIRROR_MODE_COMBOS` | y | §3.1 |
| `ZMK_MIRROR_NOTIFIER` | y when `RAW_HID` (zmk-raw-hid) is enabled | §6 |

Without zmk-raw-hid the notifier drops out with no warnings (checked).

## 6. Host notifier: `0xF4`

`app/src/mirror_notifier.c` sends the state to LayerOverlay_PEEK through zmk-raw-hid, on
the channel the keypeek (`0xFF`), modifier (`0xF2`) and dynamic-macro (`0xF3`) notifiers
use. Full spec for the overlay side: **`MIRROR_PACKET_0xF4.md`**.

| byte | value |
|---|---|
| 0 | `0xF4` |
| 1 | `6` (payload length) |
| 2 | flags: bit0 = one-shot armed |
| 3 | `overlay-id` of the instance that is on; 0 = none |
| 4..7 | its layers (union of its groups), LSB first |

- **When:** after every `zmk_mirror_mode_changed`, and after every
  `zmk_layer_state_changed` (forced, so an overlay started while a mirror is on catches up;
  Raw HID receive is off, so it can't ask).
- **Timing:** settled 8 ms and deduplicated, as `0xF2` is. Nothing is sent at boot;
  mirrors start off.
- The overlay's top layer (highest bit of the `0xFF` masks) is the firmware's highest
  active layer, so mirrored = `id != 0 && bit(top) of the mask`.
- **Why here, and in this format:** the state lives in this file, and raw-hid's header is
  visible to the whole build, so no new module is needed. The keypeek `0xFF` packet is
  fixed-length and also read by the KeyPeek app. One instance at a time makes id + mask
  the whole state.
- Excluded keys aren't sent; they belong in the exported mirror images.

## 7. Files

**New:** `app/src/mirror_mode.c`, `app/src/mirror_notifier.c`,
`app/include/dt-bindings/zmk/mirror.h`, `app/include/zmk/mirror_mode.h`,
`app/include/zmk/events/mirror_mode_changed.h`, `app/src/events/mirror_mode_changed.c`,
`app/dts/bindings/behaviors/zmk,behavior-mirror.yaml`, `app/tests/mirror-mode/` (11
cases), `app/tests/mirror-mode-combos/` (6 cases), `app/tests-host/mirror-notifier/`, this
doc.
**Modified:** `app/src/combo.c` (the two `zmk_mirror_mode_pass_next()` calls and an
include), `app/Kconfig.behaviors`, `app/CMakeLists.txt`, `PATCH_00_INDEX.md`,
`PATCH_ARCHITECTURE.md` (§7.1).

## 8. Deviations from the side-chat spec

| spec | here | why |
|---|---|---|
| `mirror-map` | **`mirror-positions`** | Zephyr's devicetree generator drops every property whose name ends in `-map` (reserved for specifier maps such as `gpio-map`): `mirror-map` simply vanished from the build |
| re-raise guard table (position, timestamp, state) | explicit hook in `combo.c` | §3.2 |
| — | `overlay-id` | the `0xF4` notifier (the notifier handoff's proposal) |
| — | `not-press-release-outputs`, `tap-ms`, `wait-ms` | same `enter`/`exit` model as caps word and the idle lock |
| optional warning if the on/off key is mirrorable | not done | can't be known statically (which layer is on top); rule 2.5 documents it |
| per-instance combo mirroring, `idle-timeout-ms`, per-group map | not done | out of scope in the spec |

## 9. Tests and verification

`app/tests/mirror-mode/` (4×4 test keyboard, two instances, layer 1 in a second group,
layer 2 unlisted; `enter`/`exit` are `&kp F13`–`F16` so edges show in the key log):

| case | checks |
|---|---|
| `1-toggle-on-off` | on: A acts as D, B as C; off: A again |
| `2-excluded-position` | an excluded key is unchanged |
| `3-unlisted-layer-not-mirrored` | holding an unlisted layer: unmirrored; released: mirrored again |
| `4-two-groups` | layer 1's group has its own exclusions |
| `5-off-while-held` | press mirrored, switch off, release: the mirrored key is released |
| `6-repeated-map-entry` | two keys mapping to one: the second is ignored, press and release, in both release orders |
| `7-momentary` | `MIRROR_MO` |
| `8-oneshot` | an excluded press doesn't use it up; the first mirrored key does, and it ends at that key's release |
| `9-switch-instances` | A on, B on: A's exit then B's enter; B's own map |
| `10-on-off-params` | `ON` twice, `OFF` twice: one edge each |
| `11-oneshot-then-on` | `ON` makes a one-shot permanent |

`app/tests/mirror-mode-combos/` (combos {2,3} and {1,2,3}, a mod-tap):

| case | checks |
|---|---|
| `1-combo-mirrored` | the mirrored twins of a combo's keys trigger it |
| `2-combo-reraise-not-mirrored-twice` | a broken 3-key combo re-raises captured events (several times, nested): none mirrored twice |
| `3-combo-reraised-keyup` | combo's duplicate key-up re-raise |
| `4-hold-tap-on-mirrored-key` | a mod-tap reached by mirroring, decided by another mirrored key |
| `5-combos-not-mirrored-kconfig` | `CONFIG_ZMK_MIRROR_MODE_COMBOS=n`: combos keep physical keys, plain keys still mirror |
| `6-hold-tap-combos-not-mirrored-kconfig` | the mod-tap case with `=n` |

- Snapshots reviewed line by line; no unexpected-re-raise warning in any log.
- **Mutation checks:** combo hook disabled → combo cases 2 and 3 fail; releases following
  the current mode → case 5 fails; no "already held" rule → case 6 fails.
- **Build-failure checks:** each of the seven §4 checks forced in a scratch keymap; each
  fails with its message, and a valid control builds.
- **Maps:** `MIRROR_MAP_GO60` equals the C default map, and is its own inverse;
  `MIRROR_MAP_GO60_FROW_SWAP` differs only in row 1.
- **Host test** `sh app/tests-host/mirror-notifier/run.sh`: the real packet code, copied
  verbatim; the bytes for off / on / one-shot / high layers, the padding, and the overlay's
  decode rule.
- Full suite (`run-test.sh all`): 296 pass, 1 pending (upstream's own `pending` case), no
  failures; 297 cases (the 281 from before plus 16 mirror cases; combo case 6 was added
  after and passes on its own). Upstream's combo tests are unchanged by the `combo.c` hook.
- **GO60 builds** (stock keymap + two instances, one on the built-in map, one on
  `MIRROR_MAP_GO60_FROW_SWAP`, a `&pixel` enter/exit): `go60_lh` and `go60_rh` clean; LH
  again with your zmk-raw-hid (`aifix`) + `raw_hid_adapter`: notifier compiled in, no
  warnings. Mirror code is absent from the RH image (central only).
- **Not done:** hardware; the notifier's timing (settle / dedupe / forced resend) has not
  been run, only built. Its bytes are tested.

## 10. Keymap migration (after flashing)

From the side-chat comparison of `layer__MIRROR` with `layer__CMKDH`:
- 36/60 keys are exact reflections; thumbs and `LH_C3R5 LH_C2R5 RH_C2R5` are unchanged →
  `excluded-positions`.
- The F-row and the `LC(N1)`/`LC(N9)` tab keys move halves in order → `MIRROR_MAP_GO60_FROW_SWAP`
  (check the tab pair).
- About 6 keys are hand-made differences (`B`/`I`/`R` vs the BBCODE / alt-tab keys,
  `&none` vs `KEY_TEST__1/2`, the TAB hold-tap at `RH_C6R2`, `LGUI K_APP` at `RH_C3R5`).
  Mirror mode has no per-key replacement: exclude them, accept them mirrored, or wait for
  an override feature. **Your call.**
- Then `LAYER__MIRROR` (27) can go, along with: the 22 combos listing it in `layers`; the
  `combo_combo_MIRROR_L` / `_R` bindings (→ `&mirror_full MIRROR_TOG`); its per-layer RGB
  entry; its `layer_listeners` entry. The colour comes from the instance's `enter`/`exit`.
- Fork-only, so inside `TESTGITHUB`; the MoErgo build keeps the old layer until then.
- Deploy: bump the fork SHA in `west.yml`, reflash both halves.
