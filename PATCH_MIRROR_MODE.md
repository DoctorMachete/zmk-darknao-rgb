# PATCH — mirror mode (`zmk,behavior-mirror`) + host notifier (`0xF4`)

**Status: implemented and tested off-hardware, not yet flashed.** 28 native_posix cases,
build-failure checks, a host test of the packet bytes, the full suite, and GO60 builds
with and without zmk-raw-hid (§9).

A mirror mode with **no layer behind it**. While an instance is on, chosen keys on chosen
layers act as their mirror key, until it's switched off. It replaces hand-kept mirror
layers (`LAYER__MIRROR`) and costs zero layers per mirrored layer. A notifier tells the
host overlay (LayerOverlay_PEEK) which mirror is on, over Raw HID.

The maps are arbitrary, so an instance is really a **relocation**: keys get one map
(`mirror-positions`), combos their own (`combo-positions`), and `&mirror-morph` (§5) gives
an instance keys of its own. Together they make "virtual layers": several base layouts
that all sit in the layer-0 slot, one at a time.

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
        combo-positions = < /* … */ >;                     // optional, §3.3
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
| `mirror-positions` | instance | GO60: `MIRROR_MAP_GO60`; elsewhere required | one entry per key position: entry *p* = the key that pressing *p* acts as. A key pointing at itself isn't mirrored. Any relocation works, not only mirrors. `POS_*` names work |
| `combo-positions` | instance | `mirror-positions` | same form, used only to match combos (§3.3). The identity map (`0 1 2 …`) keeps combos on their physical keys |
| `overlay-id` | instance | 0 | id sent to the host while this instance is on (1..255). 0 = not reported: the host sees "no mirror". Give each instance its own |
| `enter` / `exit` | instance | none | behavior chains when this instance turns on / off (also when switched away) |
| `not-press-release-outputs`, `tap-ms`, `wait-ms` | instance | off, 5, 5 | as on caps word and the idle lock: set the first for pixel feedback (constraint #9) |
| `layers` | each child | required | layers this group mirrors on |
| `excluded-positions` | each child | none | keys never mirrored while one of this group's layers is on top (by either map) |

Map macros in `dt-bindings/zmk/mirror.h`:
- `MIRROR_MAP_GO60` reflects each row (row 1: `11 10 … 0`), the row-5 keys and the thumbs.
- `MIRROR_MAP_GO60_FROW_SWAP` is the same, but row 1 swaps halves in order (`6 7 8 9 10 11
  0 1 2 3 4 5`), so F1 stays leftmost. Each half keeps its order, so a row like
  `11 10 8 9 7 6 0 1 2 3 4 5` (yours) is not this macro: write such maps out in full.

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
event's position in place** and bubbles it. Everything after it (tap-dance, the keymap)
sees the mirrored key. Its state is small: per physical key, what the press was sent as (or
"ignored"); per position, whether it's held; the active instance and mode.

### 3.1 ⚠️ Link order: right after combo

Listeners run in link order (`PATCH_ARCHITECTURE.md` §7.1). `app/CMakeLists.txt` places
`mirror_mode.c` **right after `combo.c`, before `keymap.c`**. So the events stay physical
through the idle lock, hold-tap and combo, and each of the two that cares about positions
asks mirror mode instead:

| listener | asks | gets |
|---|---|---|
| `combo.c` (matching only) | `zmk_mirror_mode_combo_position(p)` | `combo-positions[p]` (§3.3) |
| hold-tap | `zmk_mirror_mode_key_position(p, pressed)` | the key the event will act as: for a press, `mirror-positions[p]` now; for a release, what its press acted as |

Without mirror mode both are inline functions returning `p`. Checked on the LH build
(`arm-zephyr-eabi-nm -n zmk.elf | grep zmk_event_sub_.*position`): `activity`,
`behavior_hold_tap`, `behavior_caps_word`, `combo`, **`mirror_mode`**,
`behavior_tap_dance`, `keymap`.

### 3.2 Why not mirror first, and what hold-tap needs

An earlier version ran the mirror first, so hold-tap and combo saw mirrored events. Combo
re-raises some captured events from the **start** of the chain, so those went through the
mirror twice, and needed a "let the next one through" hook in `combo.c`. With the mirror
after combo, every event is mirrored exactly once: combo's re-raises reach it once, and
hold-tap's (`ZMK_EVENT_RAISE_AT(…, behavior_hold_tap)`) continue after hold-tap.

Hold-tap still has to compare positions: "is this my own key?", retro-tap, and
`hold-trigger-key-positions`. Its binding runs at the mirrored key, the event is physical,
so a mirrored mod-tap pressed with the key it was mirrored from (physical 4 acts as the
mod-tap at 7, then physical 7, acting as 4) would look like its own key and never decide.
`zmk_mirror_mode_key_position()` gives hold-tap the mirrored position for these checks
(test `mirror-mode-combos/6`). Captured events keep their physical position and are
mirrored when re-raised, with the state at that moment: a hold-tap whose hold is
`&mirror_x MIRROR_MO` mirrors the key that decided it.

`hold-trigger-key-positions` are therefore compared as the keys act (the mirrored side),
as before.

### 3.3 Combos: `combo-positions`

While an instance applies (it's on, the top layer is in one of its groups, and the key
isn't excluded there), `combo.c` matches a press of key *p* as key `combo-positions[p]`.
Only the matching changes: the event, its timestamp and its release stay physical, and
whatever combo doesn't use is mirrored once, afterwards, by `mirror-positions`.

- Without `combo-positions`, combos follow `mirror-positions`: the mirrored twins of a
  combo's keys trigger it.
- **Openers and closers.** `<` on the outer pair, `>` on the inner pair: mirrored, the pair
  would read `> <` on the other hand. Map those keys in order instead of reflected, so the
  other hand's outer pair still reaches `<`. E.g. for combos on `LH_C4R4 LH_C3R4` /
  `LH_C3R4 LH_C2R4` (38 37 / 37 36), give the right-hand keys 45 46 47 the entries
  36 37 38 (and 38 39 40 the entries 45 46 47 for combos on the right). Nothing changes in
  the combos, the editor or the MoErgo build.
- **Arbitrary relocation.** Any key can join any combo: `combo-positions` entry 9 = 2 lets
  key 9 complete a combo on 2, while the key alone still does what `mirror-positions` says.
- **Identity map** (`0 1 2 …`): combos keep their physical keys, keys still mirror. This
  replaces the old `CONFIG_ZMK_MIRROR_MODE_COMBOS=n`, and is per instance.
- `combo.c`: one line in `position_state_down()` (and an include).

### 3.4 Other notes

- **Central only** (`!ZMK_SPLIT || ZMK_SPLIT_ROLE_CENTRAL`). Right-half keys reach the
  central as position events; nothing crosses the split, and the 20-byte rule is
  untouched.
- `zmk_mirror_mode_changed` (`instance`, `overlay_id`, `active`, `oneshot`) is raised on
  every change; switching raises A off, then B on.
- Things that read keymap bindings (Studio, keypeek, the Prospector overlay) still show
  the unmirrored layout; the `0xF4` packet is how the host overlay knows (§6).
- A key that's `&trans` on the mirrored layer falls through at the **mirrored** key, since
  the event itself was rewritten.
- `&mirror-morph` and anything else bound at the mirrored key sees the mirrored position.
- Per-half RGB key-press effects see physical keys.

## 4. Build-time checks

Each fails the build with a `zmk,behavior-mirror: …` (or `zmk,behavior-mirror-morph: …`)
message (all eleven forced on purpose, §9):

| check | message (shortened) |
|---|---|
| `mirror-positions` length ≠ key count | must have one entry per key position |
| an entry ≥ key count | a `mirror-positions` entry is not a key position |
| no `mirror-positions` on a non-GO60 board | is required on this board |
| a layer in two groups of one instance | a layer is listed in two groups of the same instance |
| a `layers` entry ≥ layer count | a group's `layers` entry is not a layer of this keymap |
| an `excluded-positions` entry ≥ key count | … is not a key position |
| no child group | needs at least one child group with `layers` |
| `combo-positions` length ≠ key count | `combo-positions` must have one entry per key position |
| a `combo-positions` entry ≥ key count | a `combo-positions` entry is not a key position |
| morph: `bindings` ≠ `instances` + 1 (2 without `instances`) | `bindings` needs one entry more than `instances` |
| morph: an `instances` entry that isn't a mirror node | every `instances` entry must be a zmk,behavior-mirror node |

The same layer in different instances is fine. Two instances sharing a non-zero
`overlay-id` only logs a warning at boot.

### 4.1 Kconfig

| symbol | default | meaning |
|---|---|---|
| `ZMK_BEHAVIOR_MIRROR` | y when a `zmk,behavior-mirror` node exists (central only) | the feature |
| `ZMK_MIRROR_NOTIFIER` | y when `RAW_HID` (zmk-raw-hid) is enabled | §6 |
| `ZMK_BEHAVIOR_MIRROR_MORPH` | y when a `zmk,behavior-mirror-morph` node exists | §5 |
| `ZMK_BEHAVIOR_MIRROR_MORPH_MAX_HELD` | 4 | keys held at once per morph node |

`ZMK_MIRROR_MODE_COMBOS` is gone: use an identity `combo-positions` (§3.3). Without
zmk-raw-hid the notifier drops out with no warnings (checked).

## 5. `&mirror-morph`: keys of an instance's own

Like mod-morph, but it picks its binding from the mirror instance that applies when the key
is pressed (on, and the top layer in one of its groups):

```dts
#include <dt-bindings/zmk/mirror.h>

/ { behaviors {
    mm_q: mm_q {                                   // any instance: bindings[1]
        compatible = "zmk,behavior-mirror-morph";
        #binding-cells = <0>;
        bindings = <&kp Q>, <&kp SEMI>;
    };
    mm_k: mm_k {                                   // per instance: one binding each
        compatible = "zmk,behavior-mirror-morph";
        #binding-cells = <0>;
        instances = <&layout_a &layout_b>;
        bindings = <&kp K>, <&kp E>, <&kp N>;      // default, layout_a, layout_b
    };
}; };
```

- Without `instances`: `bindings = <DEFAULT MIRRORED>`, the second whenever any instance
  applies. With `instances`: one binding more than instances; an instance not in the list,
  or none, gives the default.
- The release goes to the binding that was pressed, kept per key position, so switching
  instances (or a `MIRROR_MO` released first) while it's held never strands a key.
- Works on any key, as a combo binding, and inside hold-taps, like any zero-parameter
  behavior. On a mirrored key it is looked up at the mirrored position (where the keymap
  has it).
- Any behavior can be a binding (`&kp`, `&mt`, `&mo`, a macro…).

**Virtual layers.** Several base layouts that all sit in the layer-0 slot: give each one an
instance whose group lists that layer, with an identity `mirror-positions` (nothing moves)
or a real relocation, its own `combo-positions`, and `&mirror-morph` on the keys a pure
relocation can't express. Switch with `&layout_a MIRROR_ON`: only one instance is on at a
time, so switching to another is one key press, and `overlay-id` tells the host overlay
which one is on. Layers above them (NAV, SYM…) work as usual: they're either listed in a
group (so they follow the layout) or not (so they don't).

`app/src/behaviors/behavior_mirror_morph.c`, `zmk,behavior-mirror-morph.yaml`. It reads
`zmk_mirror_mode_applying_instance()`; with mirror mode compiled out that's NULL, and
every morph gives its default.

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
`app/src/behaviors/behavior_mirror_morph.c`, `app/include/dt-bindings/zmk/mirror.h`,
`app/include/zmk/mirror_mode.h`, `app/include/zmk/events/mirror_mode_changed.h`,
`app/src/events/mirror_mode_changed.c`, `app/dts/bindings/behaviors/zmk,behavior-mirror.yaml`,
`app/dts/bindings/behaviors/zmk,behavior-mirror-morph.yaml`, `app/tests/mirror-mode/` (11
cases), `app/tests/mirror-mode-combos/` (6), `app/tests/mirror-mode-combo-positions/` (6),
`app/tests/mirror-mode-morph/` (5), `app/tests-host/mirror-notifier/`, this doc.
**Modified:** `app/src/combo.c` (one line in `position_state_down()`, an include),
`app/src/behaviors/behavior_hold_tap.c` (one line at the top of its position listener, an
include), `app/Kconfig.behaviors`, `app/CMakeLists.txt`, `PATCH_00_INDEX.md`,
`PATCH_ARCHITECTURE.md` (§7.1).
**Removed (2026-11-01):** `combo-pairs`, `MIRROR_WITH` / `MIRROR_NEXT`,
`CONFIG_ZMK_MIRROR_MODE_COMBOS`, the combo re-raise hook, `app/tests/mirror-mode-combo-pairs/`.

## 8. Deviations from the side-chat spec

| spec | here | why |
|---|---|---|
| `mirror-map` | **`mirror-positions`** | Zephyr's devicetree generator drops every property whose name ends in `-map` (reserved for specifier maps such as `gpio-map`): `mirror-map` simply vanished from the build |
| re-raise guard table (position, timestamp, state) | not needed: the listener runs after combo | §3.2 |
| — | `overlay-id` | the `0xF4` notifier (the notifier handoff's proposal) |
| — | `not-press-release-outputs`, `tap-ms`, `wait-ms` | same `enter`/`exit` model as caps word and the idle lock |
| — | `combo-positions` | combos get their own map (user request, 2026-11-01; replaces the 2026-10-30 `combo-pairs`); §3.3 |
| — | `&mirror-morph` | per-instance keys, for virtual layers (user request, 2026-11-01); §5 |
| optional warning if the on/off key is mirrorable | not done | can't be known statically (which layer is on top); rule 2.5 documents it |
| per-instance combo mirroring | `combo-positions` per instance | §3.3 |
| `idle-timeout-ms`, per-group map | not done | out of scope in the spec |

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

`app/tests/mirror-mode-combos/` (combos {2,3} and {1,2,3}, a mod-tap; no
`combo-positions`, so combos follow `mirror-positions`):

| case | checks |
|---|---|
| `1-combo-mirrored` | the mirrored twins of a combo's keys trigger it |
| `2-combo-reraise-not-mirrored-twice` | a broken 3-key combo re-raises captured events (several times, nested): each mirrored once |
| `3-combo-reraised-keyup` | combo's duplicate key-up re-raise |
| `4-hold-tap-on-mirrored-key` | a mod-tap reached by mirroring, decided by another mirrored key |
| `5-identity-combo-positions` | identity `combo-positions`: combos keep physical keys, plain keys still mirror |
| `6-hold-tap-with-its-mirror-key` | the mod-tap (at 7, via physical 4) decided by physical 7: Shift+E, not E then H (§3.2) |

`app/tests/mirror-mode-combo-positions/` (keys reflect rows 0–1; combos keep row 0, reflect
row 1, and keys 9/10 act as 2/3; combos {0,1}, {2,3}, {4,5}):

| case | checks |
|---|---|
| `1-row-kept-for-combos` | physical 0+1 give combo {0,1}, not its mirror |
| `2-single-key-still-mirrored` | a lone key: combo candidate, times out, mirrored once by `mirror-positions` |
| `3-reflected-row-for-combos` | physical 6+7 reach {4,5}; physical 4+5 reach nothing and mirror as keys |
| `4-arbitrary-relocation` | off: 9+10 are J K; on: they reach combo {2,3} |
| `5-relocated-key-alone` | key 9 alone: J (its own `mirror-positions` entry) |
| `6-excluded-key-not-relocated` | an excluded key isn't relocated for combos either |

`app/tests/mirror-mode-morph/` (two instances with identity maps; morphs with and without
`instances`; a combo bound to a morph; layer 1 unlisted):

| case | checks |
|---|---|
| `1-off-defaults` | off: every morph gives its default |
| `2-per-instance` | A on, B on, off: each morph's binding for each |
| `3-unlisted-layer-default` | unlisted layer on top: default; released: the instance's binding |
| `4-switch-while-held` | switching instances, or a `MIRROR_MO` released first, while held: the pressed binding is released |
| `5-as-combo-binding` | a morph as a combo's binding |

- Snapshots reviewed line by line.
- **Mutation checks:** releases following the current mode → case 5 fails; no "already
  held" rule → case 6 fails (both from before); combo matching on the physical key → two
  combo-positions cases fail; `combo-positions` ignored (→ `mirror-positions`) → two fail;
  exclusion ignored for combos → case 6 fails; morph releasing the binding for the current
  state → morph 4 fails; per-instance lookup always giving the first instance's binding →
  morph 2 fails. Case `mirror-mode-combos/6` fails without the hold-tap line (that's how
  the problem was found).
- **Build-failure checks:** each §4 check forced in a scratch keymap (the four new ones on
  2026-11-01); each fails with its message, and a valid control builds.
- Upstream `app/tests/combo/` and `app/tests/hold-tap/` pass with the changes.
- **Host test** `sh app/tests-host/mirror-notifier/run.sh`: the real packet code, copied
  verbatim; the bytes for off / on / one-shot / high layers, the padding, and the overlay's
  decode rule.
- Full suite (`run-test.sh all`, 2026-11-01): 309 cases, 308 pass, 1 pending (upstream's
  own `pending` case), no failures.
- **GO60 builds** (stock keymap + two instances, one on the built-in map with a full
  `combo-positions` and your bracket combos as real combos, one on
  `MIRROR_MAP_GO60_FROW_SWAP`, a `&pixel` enter/exit, and a `&mirror-morph` with
  `instances` on a key): `go60_lh` and `go60_rh` clean; LH again with your zmk-raw-hid
  (`aifix`) + `raw_hid_adapter`: notifier compiled in, no warnings. Listener order checked
  in the LH image (§3.1). Mirror code is absent from the RH image (central only).
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
  Exclude them, accept them mirrored, or give them a `&mirror-morph` (§5) in the base
  layer (fork-only, so inside `TESTGITHUB`). **Your call.**
- Combos: replace `combo-pairs` with `combo-positions` (§3.3).
- Then `LAYER__MIRROR` (27) can go, along with: the 22 combos listing it in `layers`; the
  `combo_combo_MIRROR_L` / `_R` bindings (→ `&mirror_full MIRROR_TOG`); its per-layer RGB
  entry; its `layer_listeners` entry. The colour comes from the instance's `enter`/`exit`.
- Fork-only, so inside `TESTGITHUB`; the MoErgo build keeps the old layer until then.
- Deploy: bump the fork SHA in `west.yml`, reflash both halves.
