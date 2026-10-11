# PATCH — layer idle-lock (`zmk,layer-idle-lock`)

**Status: implemented; 12 new native_posix tests and the full ZMK test suite pass, and
`go60_lh` / `go60_rh` build (§8). Not yet flashed.** Lets a layer held by `&mo` (directly,
or as the hold binding of a hold-tap) lock itself on after the key has been held for
`idle-ms` with no other key activity, and fires behavior chains as feedback. Once locked,
releasing the key leaves the layer on; `&tog` / `&to` turn it off.

Shared background: `PATCH_ARCHITECTURE.md`. Typical consumer of `PATCH_PIXEL.md` /
`PATCH_PIXLBLINK.md`.

---

## 1. What it does

The GO60 case: the LH thumb `POS_LH_T3` is `&HTUI_mo_kp_bal_250_200_50 LAYER__LNPAD DEL`
(hold = `&mo LAYER__LNPAD`), and inside LNPAD that same position is `&tog LAYER__LNPAD`.

| you do | firmware |
|---|---|
| hold the thumb past the tapping term | hold-tap decides *hold* → `&mo` activates LNPAD → per-layer RGB on → **idle clock starts** |
| type digits | every press/release restarts the clock; it stops while a key is held |
| keep holding, touch nothing for `idle-ms` | **LNPAD locks**, `enter` fires (e.g. `&pixel PIXEL_LH_T3 PX_WHITE`) |
| release the thumb | `&mo` release → the keymap ignores the deactivation; LNPAD stays on |
| type | still LNPAD |
| tap the thumb | resolves in LNPAD → `&tog LAYER__LNPAD` → layer off, lock cleared, `exit` fires (`&pixel PIXEL_LH_T3 (-1)`), per-layer RGB off |
| (or) release the thumb before `idle-ms` | ordinary momentary layer; nothing fires |

The release of a held key always goes to the binding resolved at **press** time
(`zmk_keymap_active_behavior_layer[]` in `keymap.c`), so putting `&tog` at the thumb
position inside LNPAD does not disturb the hold: the release still reaches the base-layer
hold-tap and its `&mo`.

## 2. Shape: a lock in the keymap, plus a locker

Two pieces, deliberately separate:

1. **Layer locks in `keymap.c`** — a `_zmk_keymap_layer_locks` bitmask with the same
   meaning as upstream ZMK's: a locked layer ignores ordinary deactivation.
2. **The idle locker** — `app/src/layer_idle_lock.c`, configured by a
   `zmk,layer-idle-lock` devicetree node. It decides *when* to lock and fires the
   feedback. It is today the only thing that creates locks.

### 2.1 Relation to upstream ZMK layer locking

Upstream (`zmkfirmware/zmk` main) added layer locking after the 24.12 base this fork
sits on: `&tog` and `&to` lock the layer they turn on, and a locked layer can then only
be turned off by `&tog` / `&to` — so `&tog 1` tapped while holding `&mo 1` keeps layer 1
on after the release. Custom `&mo`/`&tog`/`&to` instances opt in with `locking;`.

| | upstream | this fork |
|---|---|---|
| lock state | `_zmk_keymap_layer_locks` | same |
| what a lock blocks | every non-locking deactivation | `zmk_keymap_layer_deactivate()`: the same set (`&mo`, `&sl`, conditional layers, temp-layer, modules) |
| what overrides a lock | `&tog`, `&to` | `&tog` (flip and `toggle-mode = "off"`), `&to` |
| who creates locks | `&tog` / `&to` turning a layer on; `locking;` | **only the idle lock** |
| `&tog` on an active, unlocked layer | locks it (stays on) | turns it off (24.12 behaviour, unchanged) |
| API | `activate/deactivate/toggle/to(layer, bool locking)` | **signatures unchanged**; adds `zmk_keymap_layer_lock/unlock/locked/locks()` |
| lock change raises `layer_state_changed` | yes | no |

Why not port upstream verbatim:

- **Signatures.** Adding `bool locking` to `zmk_keymap_layer_activate()` and friends
  breaks every out-of-tree module that calls them. Eleven modules are pulled from
  `west.yml`, most of them private, so they cannot all be audited.
- **`&tog` semantics.** Making `&tog` / `&to` locking changes what every existing `&tog`
  in a 5,000-line keymap does next to an `&mo`. The idle lock needs none of that.
- **No event on lock.** Upstream raises `layer_state_changed` when only the lock bit
  changes. Here that would reach `zmk-listeners`, per-layer RGB and keypeek as a second
  "layer on" for a layer that never went off. The locker fires its own `enter` instead.

**Porting to an upstream-based branch** (`rgb-layer-25.x`): drop this file's `keymap.c`,
`keymap.h`, `behavior_toggle_layer.c` hunks, and replace the locker's single call
`zmk_keymap_layer_lock(layer)` with `zmk_keymap_layer_activate(layer, true)`. The locker
already ignores `layer_state_changed` events with `state == true`, so upstream's extra
lock event is harmless to it.

## 3. The model

```dts
/ {
    layer_idle_lock {
        compatible = "zmk,layer-idle-lock";
        /* tap-ms = <5>; wait-ms = <5>;   queued path only, same as zmk-listeners */

        lock_lnpad {
            layer = <LAYER__LNPAD>;
            idle-ms = <2000>;
            not-press-release-outputs;
            enter = <&pixel PIXEL_LH_T3 PX_WHITE>;
            exit  = <&pixel PIXEL_LH_T3 (-1)>;
        };
    };
};
```

| property | where | default | meaning |
|---|---|---|---|
| `layer` | child, **required** | — | the layer to lock. One layer per child; the first child naming a layer wins |
| `idle-ms` | child | 2000 | idle time before locking. `0` locks as soon as `&mo` holds the layer |
| `enter` | child | none | chain fired when the lock engages |
| `exit` | child | none | chain fired when the locked layer is turned off. **Not** fired for a plain momentary release |
| `not-press-release-outputs` | child | off | same name and meaning as in `zmk-listeners` and caps word. **Set it for `&pixel` / `&pixlblink`** |
| `tap-ms`, `wait-ms` | parent | 5, 5 | queued path only |

`enter` / `exit` are ordinary chains: several behaviors fire in order, no macro needed.
Only the first node with this compatible is used.

### 3.1 What counts as "idle"

- The clock **arms** when `&mo` activates the layer. For a hold-tap that is the hold
  decision (tapping term, or the balanced "other key up"), not the physical press.
- **Every key press or release restarts it.** Idle is measured from the last release.
- It **stops while any key is held** that was not already down when the layer armed.
  Holding a digit or `BSPC` to auto-repeat never locks the layer.
- Keys already down at arm time — the `&mo` key itself, a modifier you were holding —
  are *baseline*: holding them is not activity. A baseline key leaves the baseline once
  released, so pressing it again counts like any other key.
- Both halves count (the central sees every position).
- **Pointer motion counts too**: any Zephyr input event (either trackpad, mouse-key
  behaviors) restarts the clock, so moving the cursor while holding the layer key
  never locks the layer. It cannot *stop* the clock the way a held key does; it only
  pushes the deadline out. Encoders and host activity do not count.

### 3.2 State machine (per child)

```
            &mo press                 idle-ms elapsed
  IDLE ─────────────────▶ ARMED ───────────────────────▶ LOCKED   (enter fires)
   ▲                        │                               │
   │      &mo release       │                               │ layer turned off
   └────────────────────────┘                               │ by &tog / &to
   ▲                                                        │ (exit fires)
   └────────────────────────────────────────────────────────┘
```

- `&mo` pressed again on a LOCKED layer: stays locked (as upstream: `&mo` cannot release a
  lock). `&mo` released while LOCKED: nothing; the keymap ignores the deactivation.
- A layer turned on by `&tog` (e.g. the MoErgo-compatible combo) is never armed.
- Any layer-off while ARMED disarms silently.

### 3.3 Getting out of a locked layer

`&tog L` (flip, or `toggle-mode = "off"`), or `&to N`. Not `&mo`, not `&sl`, not a
conditional layer, not a module calling `zmk_keymap_layer_deactivate()`.

**Put `&tog L` at the `&mo` key's own position inside layer L.** Without it, a tap of
that key resolves to the base-layer hold-tap (tap = `DEL` here) and the only exit is a
`&tog` / `&to` somewhere else. The existing `comboToggleLNPAD` / `comboToggleRNPAD`
combos list their own layer in `layers`, so they also work as an exit.

## 4. Firmware

### 4.1 Core lock — `app/src/keymap.c`, `app/include/zmk/keymap.h`

- `static zmk_keymap_layers_state_t _zmk_keymap_layer_locks`.
- `set_layer_state()`: a deactivation of a locked layer returns 0 without changing state
  or raising an event. The check sits after the default-layer guard and before the state
  write. A deactivation that does go through can only reach a layer whose lock is clear.
- `zmk_keymap_layer_toggle()` unlocks before deactivating. `zmk_keymap_layer_to()` unlocks
  each layer before deactivating it.
- New: `zmk_keymap_layer_locks()`, `zmk_keymap_layer_locked()`,
  `zmk_keymap_layer_lock()` (−EINVAL unless the layer's bit is set; the always-active
  default layer therefore cannot be locked), `zmk_keymap_layer_unlock()`.
- Invariant: a lock bit implies the matching state bit.

### 4.2 Behaviors

- `behavior_toggle_layer.c`: `toggle-mode = "off"` unlocks first. Flip goes through
  `zmk_keymap_layer_toggle()`; `"on"` is unchanged.
- `behavior_momentary_layer.c`: press calls `zmk_layer_idle_lock_momentary_pressed()`
  after activating; release calls `zmk_layer_idle_lock_momentary_released()` before
  deactivating. Without a node these are empty inlines (`zmk/layer_idle_lock.h`).
- `&to` needs no change: it calls `zmk_keymap_layer_to()`.

### 4.3 The locker — `app/src/layer_idle_lock.c`

- One `struct lil_state` per child: config pointer, phase, baseline bitmap, and a
  `k_work_delayable` on the system work queue. Position events, layer events, behavior
  callbacks and hold-tap timers all run there too, so there are no races to guard.
- One module-wide `held[]` bitmap of pressed positions (`ZMK_KEYMAP_LEN` bits).
- Listens to `zmk_position_state_changed` (activity) and `zmk_layer_state_changed`
  (`state == false` only: disarm, and fire `exit` if it was LOCKED).
- With `CONFIG_INPUT`, an `INPUT_CALLBACK_DEFINE(NULL, …)` callback restarts the timer of
  every ARMED child on any input event. It runs on the **input thread**, so it touches
  nothing but `k_work_reschedule()` (thread-safe); the timer handler's re-check makes a
  reschedule that races a disarm or a key press harmless.
- `lil_fire()` is the caps-word firing code: `behavior_keymap_binding_pressed()` on the
  not-press-release path (constraint #9), behavior queue otherwise.
- The timer handler re-checks phase and busy before locking.
- `BUILD_ASSERT`s: `layer` < number of keymap layers, `idle-ms` ≥ 0, ≤ 255 bindings per
  chain.

### 4.4 ⚠️ Link order is load-bearing

ZMK runs event subscriptions in **link order** (`.event_subscription` is a plain
`KEEP()` section, not sorted). `layer_idle_lock.c` is listed **first** in the central block
of `app/CMakeLists.txt`, so it sees every press and release as it physically happens —
before the hold-tap and combo listeners can capture an event and replay it later. Checked
in the built binary: `activity → layer_idle_lock → behavior_hold_tap → keymap`.

Out-of-tree modules add their sources to the same `app` target. A module listener that
*captures* position events and is linked earlier could hide a press or release from the
locker. The failure mode is safe: a press seen without its release leaves that position
"held", so the layer just does not lock until that key is pressed and released again.

### 4.5 Build plumbing

- `app/Kconfig` (Keymaps menu): `ZMK_LAYER_IDLE_LOCK`, `default y`, depends on
  `DT_HAS_ZMK_LAYER_IDLE_LOCK_ENABLED` and on being the central (or not split). The node's
  presence is the switch; nothing goes in `go60.conf`.
- `app/dts/bindings/zmk,layer-idle-lock.yaml`.

## 5. Constraints

### 5.1 Architecture constraints touched

- **#1 (20-byte split payload): untouched.** Nothing new crosses the split. Lock state
  lives on the central; the peripheral sees ordinary layer state (per-layer RGB keeps
  showing LNPAD while it is locked) and ordinary `&pixel` writes.
- **#4 (node must exist):** not a behavior, so the rule doesn't bite: no node ⇒ Kconfig
  off ⇒ `&mo` compiles against the empty inlines.
- **#5:** the node is its own top-level node. Not inside `behaviors { }`, and **never
  inside `layer_listeners { }`** (that module would compile it as a listener).
- **#6 (`TESTGITHUB`):** the node must be guarded. `&pixel` is an unresolved label in the
  MoErgo build.
- **#9:** set `not-press-release-outputs` for pixel behaviors.
- **Deploy ritual:** fork change ⇒ bump the SHA in `west.yml`, rebuild, reflash both halves.

### 5.2 New hazards

1. **`&sl` is built on `&mo`.** A sticky layer on a configured layer arms the clock too.
   If `idle-ms` is shorter than the sticky key's `release-after-ms`, an untouched sticky
   layer can lock. Not an issue for LNPAD/RNPAD.
2. **A locked layer resists every non-`&tog`/`&to` deactivation**, including modules and
   conditional layers. If one of them is supposed to switch it off, it won't while locked.
3. **Two `&mo` keys on the same layer:** releasing either disarms (and, as in stock 24.12,
   turns the layer off unless it is locked).

## 6. Keymap wiring (GO60)

Two changes, both in `go60.keymap`.

**a) The node** — inside an existing `#ifdef TESTGITHUB` region of the custom-behaviors
area, as a **sibling** of `layer_listeners { }` (not inside it):

```dts
#ifdef TESTGITHUB
  layer_idle_lock {
    compatible = "zmk,layer-idle-lock";
    lock_lnpad {
      layer = <LAYER__LNPAD>;
      idle-ms = <2000>;
      not-press-release-outputs;
      enter = <&pixel PIXEL_LH_T3 PX_WHITE>;
      exit  = <&pixel PIXEL_LH_T3 (-1)>;
    };
    lock_rnpad {
      layer = <LAYER__RNPAD>;
      idle-ms = <2000>;
      not-press-release-outputs;
      enter = <&pixel PIXEL_RH_T3 PX_WHITE>;
      exit  = <&pixel PIXEL_RH_T3 (-1)>;
    };
  };
#endif
```

`PIXEL_LH_T3` (strip 2) and `PIXEL_RH_T3` (strip 32) light exactly `POS_LH_T3` (56) and
`POS_RH_T3` (57) — checked against the `pixel-lookup` tables in `go60_lh.dts` /
`go60_rh.dts`. So the key that lights is the key that now unlocks. Neither pixel is used
by caps word (`*_T2`), the dynamic-macro slots, or the MAGIC indicators. A blink works
the same way: a `&pixlblink` preset in `behaviors { }`, `exit = <&pixlblink_off …>`.

**b) The exit key** — in `layer__LNPAD`, `POS_LH_T3` from `&trans` to
`&tog LAYER__LNPAD`; in `layer__RNPAD`, `POS_RH_T3` from `&trans` to
`&tog LAYER__RNPAD`. These are plain ZMK, so set them in the MoErgo editor.

> **Effect on the MoErgo build** (no lock): holding the thumb works as before. With the
> layer toggled on by the combo, a tap of that thumb now exits the layer instead of
> sending `DEL` / `BSPC`. Both layers already have `DEL` / `BSPC` elsewhere.

## 7. Files

**New (4):** `app/src/layer_idle_lock.c`, `app/include/zmk/layer_idle_lock.h`,
`app/dts/bindings/zmk,layer-idle-lock.yaml`, this doc.
**Modified (6):** `app/src/keymap.c`, `app/include/zmk/keymap.h`,
`app/src/behaviors/behavior_momentary_layer.c`, `app/src/behaviors/behavior_toggle_layer.c`,
`app/CMakeLists.txt`, `app/Kconfig`.
**Tests (new):** `app/tests/layer-idle-lock/` (12 cases).
**Docs touched:** `PATCH_00_INDEX.md`, `PATCH_ARCHITECTURE.md` (§7 link-order note).

## 8. Verification

This session had a full west workspace (`zmkfirmware/zephyr v3.5.0+zmk-fixes`, SDK 0.16.9),
so the checks below are real builds and runs, not inspection.

**ZMK's own test harness** (`app/run-test.sh`, `native_posix_64`, simulated time, mocked
key matrix). New suite `tests/layer-idle-lock/`, snapshots reviewed line by line:

| case | checks |
|---|---|
| 1 lock-then-tog-off | hold past idle → lock + `enter`; release keeps layer; `&tog` at the same position → off + `exit` |
| 2 release-before-idle | released early: plain momentary, nothing fires later |
| 3 typing-restarts-clock | a tap restarts the clock; release before the new deadline → no lock |
| 4 held-key-blocks-lock | a key held longer than `idle-ms` never locks |
| 5 type-then-idle-then-to | lock 500 ms after the last release (t=213 → 714 ms); `&to 0` unlocks + `exit` |
| 6 key-held-before-mo | a key already down when armed is baseline: still locks |
| 7 tog-on-is-not-armed | `&tog` on → no lock, no `exit` on `&tog` off |
| 8 hold-tap-hold-locks | GO60 shape: tap = `DEL`; hold → `&mo` → lock; thumb tap in layer = `&tog` → off |
| 9 hold-tap-balanced-typed-then-idle | balanced hold decided by a typed key; replayed key lands in the layer; lock 500 ms after the decision |
| 10 hold-tap-released-early | hold decided, released before idle → momentary |
| 11 second-mo-and-tog-off | a second `&mo` pressed/released on a locked layer changes nothing; `toggle-mode = "off"` unlocks + `exit` |
| 12 pointing-is-activity | `&mmv` held as baseline keeps moving the cursor: no lock until 500 ms after motion stops (t=1023 → 1524 ms; without the input hook it would lock at 522 ms) |

**Regression:** the full suite (`run-test.sh all`: 244 upstream cases plus the first 10
new ones) on the patched tree: 252 pass; 1 is an upstream case marked `pending` upstream
(`modifiers/implicit/kp-mod1-dn-mod2-dn-mod1-up-mod2-up`); 1 new case failed to *build*
because it was compiled in the middle of an edit, and passes on rebuild. After the last
code change, all 12 idle-lock cases and the `momentary-layer`, `toggle-layer` and
`to-layer` suites were re-run: all pass.

**Target builds:** `go60_lh` and `go60_rh` built with the fork's stock GO60 keymap plus a
`zmk,layer-idle-lock` node using `&pixel` / `&pixlblink` on both halves, and with
`RGB_UNDERGLOW`, `EXPERIMENTAL_RGB_LAYER`, `MAGIC_INDICATOR` on. No new warnings. On the
central, the binary has the locker, the input callback, and the listener order
`activity → layer_idle_lock → behavior_hold_tap → … → keymap`. The peripheral build
compiles the locker out (Kconfig depends on the central role).

**Not verifiable here:** the real `go60.keymap` (it needs private modules), and the
hardware. CI plus a flash is still the real test.
