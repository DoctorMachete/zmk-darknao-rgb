# PATCH — layer locking (upstream ZMK semantics)

**Status: working on hardware** (GO60, first test of the GitHub build, 2026-10-12; the
MoErgo build of the same keymap compiles). A port of upstream
ZMK main's layer locking to this 24.12-based fork. Stock `&tog` and `&to` now **lock**
the layer they turn on, and a locked layer can only be turned off by a locking
behavior. `&mo` stays non-locking. Custom behavior instances opt out by leaving out
`locking;`.

Supersedes the private lock API that `PATCH_LAYER_IDLE_LOCK.md` used to add
(`zmk_keymap_layer_lock()` / `_unlock()`). The idle lock now locks through this API.

Shared background: `PATCH_ARCHITECTURE.md` (§7.1 link order, §7.2 layer locks).

---

## 1. What changes for a keymap

| you do | before (24.12) | now (upstream semantics) |
|---|---|---|
| `&tog L` with L off | L on | L on **and locked** |
| `&tog L` with L on **and locked** | L off | L off, unlocked |
| `&tog L` with L on but **not locked** (held by `&mo`, `&sl`, a conditional layer, the trackpad's temp layer) | L off | **L locked, stays on**. A second tap turns it off |
| `&to L` | all others off, L on | all others off and unlocked, L on **and locked** |
| `&mo L` press / release | on / off | on / off, **except** the release cannot turn off a locked L |
| `&sl L` (built on `&mo`) | on / off | same as `&mo` |
| conditional layers, `zip_temp_layer`, out-of-tree modules | on / off | on / off, but cannot turn off a locked layer |

Rule of thumb: **whatever a locking behavior turned on, only a locking behavior turns
off.** The typical use (upstream's reason for the feature): hold `&mo L`, tap a `&tog L`
inside L, release — L stays on.

`toggle-mode` variants follow the same rule: `"on"` with `locking;` turns on and locks
(also locks a layer that is already on); `"off"` with `locking;` turns off and unlocks
whatever the layer's state.

## 2. Opting out, opting in

Locking is a per-instance property, as upstream:

| instance | locking? |
|---|---|
| stock `&tog` (`toggle_layer.dtsi`) | **yes** (`locking;`) |
| stock `&to` (`to_layer.dtsi`) | **yes** (`locking;`) |
| stock `&mo` | no |
| any **custom** instance of `zmk,behavior-toggle-layer` / `-to-layer` / `-momentary-layer` | **only if it has `locking;`** |

So the opt-out is a custom instance without the property — the 24.12 behaviour:

```dts
/ {
    behaviors {
#ifdef TESTGITHUB   // `toggle-mode` / `locking` may not exist in MoErgo's firmware
        tog_nl: tog_nl {                         // plain toggle, never locks
            compatible = "zmk,behavior-toggle-layer";
            #binding-cells = <1>;
        };
        tog_off: tog_off {                       // "always off", even if locked
            compatible = "zmk,behavior-toggle-layer";
            #binding-cells = <1>;
            toggle-mode = "off";
            locking;
        };
#endif
    };
};
```

Note the opt-out's limit: `&tog_nl L` turns off an *unlocked* L, but cannot turn off an L
that something else locked. For a key whose only job is "leave L, whatever happened",
use a locking `toggle-mode = "off"` instance (`&tog_off` above).

### 2.1 MoErgo-safe exit key (as wired in `go60.keymap`)

The bindings in the layer grid must also compile in the MoErgo build, where `tog_exit`
doesn't exist. The GO60 keymap keeps one binding in the editor and lets the preprocessor
pick the behavior:

```dts
/* inside behaviors { }, in the #ifdef TESTGITHUB region (next to blink_LNPAD) */
        tog_exit: tog_exit {                 // always turns the layer OFF, locked or not
            compatible = "zmk,behavior-toggle-layer";
            #binding-cells = <1>;
            toggle-mode = "off";
            locking;
        };
        #define TOG_EXIT tog_exit

/* in the fallback block (next to #ifndef SMI_PARBRACELEFT) */
  #ifndef TOG_EXIT
    #define TOG_EXIT tog
  #endif

/* editor macro, so the layer stays a GUI parameter */
        macroUI_TOG_EXIT: macroUI_TOG_EXIT {
            compatible = "zmk,behavior-macro-one-param";
            #binding-cells = <1>;
            tap-ms = <0>;
            wait-ms = <0>;
            bindings = <&macro_param_1to1>, <&TOG_EXIT MACRO_PLACEHOLDER>;
        };
```

Keys: `&macroUI_TOG_EXIT LAYER__MOUSE`. The `MACRO_PLACEHOLDER` is required: without it
the build fails with `missing data after phandle in … bindings`. GitHub build: one tap
closes MOUSE whether the trackpad opened it or it is locked. MoErgo build: plain
`&tog LAYER__MOUSE`. Both builds compile; the GitHub build is tested on hardware.

A `&mo` instance with `locking;` locks on press and unlocks on release (and its release
turns off the layer even if `&tog` locked it). Supported because upstream has it; not
used here.

## 3. Differences from upstream

Behaviour is upstream's. Two interface differences, both deliberate:

| | upstream main | this fork | why |
|---|---|---|---|
| layer API | `zmk_keymap_layer_activate/deactivate/toggle/to(layer, bool locking)` | same functions named `*_ex(layer, locking)`; the **one-argument** 24.12 functions are kept and are the non-locking variants | eleven out-of-tree modules are pulled from `west.yml`, most private; changing the signatures would break any that call them. As non-locking callers they get exactly what upstream gives `locking = false` |
| event on a lock-only change | re-raises `zmk_layer_state_changed` | raises the new **`zmk_layer_lock_changed`** (`layer`, `locked`) | a second `layer_state_changed(on)` for a layer that never went off would make `zmk-listeners` layer listeners fire `enter` twice (per-layer RGB, keypeek, the `rgb_on_off` group …) |

Also as upstream: `zmk_keymap_layer_locks()` and `zmk_keymap_layer_locked()`. The
always-active default layer reads as locked, and `&to 0` sets its lock bit (harmless:
the default layer cannot be turned off anyway).

Order of events: a locking activation of an off layer raises `layer_state_changed(on)`
then `layer_lock_changed(locked)`; a locking deactivation raises
`layer_state_changed(off)` then `layer_lock_changed(unlocked)`; locking a layer that is
already on raises only `layer_lock_changed`.

**Porting to an upstream-based branch** (`rgb-layer-25.x`): drop this patch's `keymap.c`,
`keymap.h`, behavior, binding and dtsi hunks and the `layer_lock_changed` event; rename the
`*_ex` calls in `layer_idle_lock.c` to upstream's two-argument functions; have the idle
lock listen to `layer_state_changed` for lock changes (upstream re-raises it) and read
`zmk_keymap_layer_locked()`.

## 4. Firmware

- **`app/src/keymap.c`**: `_zmk_keymap_layer_locks`; `set_layer_state(layer, state,
  locking)` ignores a non-locking deactivation of a locked layer, and with `locking`
  writes the lock bit together with the state bit; `toggle_ex()` deactivates only if the
  layer is active **and** (non-locking or locked), otherwise activates; `to_ex()`
  deactivates every layer with the same `locking`, then activates the target. The
  one-argument functions call the `_ex` ones with `false`.
- **`app/include/zmk/keymap.h`**: the `_ex` declarations, `locks()`, `locked()`.
- **`app/include/zmk/events/layer_lock_changed.h`**, **`app/src/events/layer_lock_changed.c`**:
  the event; built unconditionally (`app/CMakeLists.txt`, next to `layer_state_changed.c`).
- **Behaviors**: `behavior_toggle_layer.c`, `behavior_to_layer.c`,
  `behavior_momentary_layer.c` read `locking` per instance
  (`DT_INST_PROP_OR(n, locking, false)`). `&to` and `&mo` became multi-instance (they
  were single-instance in 24.12), so custom instances of them work.
- **Bindings**: `locking` (boolean) on the three compatibles.
- **`app/dts/behaviors/toggle_layer.dtsi`, `to_layer.dtsi`**: `locking;`.
- Not touched (already non-locking callers, as upstream passes `false`):
  `conditional_layer.c`, `pointing/input_processor_temp_layer.c`, sticky keys.

## 5. Hazards

1. **`&tog` on a layer that something else is holding locks instead of closing it.** In
   the GO60 keymap: the `&tog LAYER__MOUSE` keys inside MOUSE, when MOUSE was opened by
   the trackpad (`zip_temp_layer`): the first tap locked, the second closed. **Resolved**
   with the locking `toggle-mode = "off"` exit key of §2.1 (`macroUI_TOG_EXIT`).
2. **A locked layer ignores every non-locking switch-off**: `&mo` / `&sl` release,
   conditional layers, `zip_temp_layer` timeouts, modules. E.g. after a double tap of
   `td_LAYER__NUM` (`&to LAYER__NUM`), a single tap of it (`&mo LAYER__NUM`) no longer
   leaves NUM; `&to LAYER__CMKDH` does.
3. **MoErgo builds differ.** MoErgo's firmware has no layer locking, so the same keymap
   behaves the 24.12 way there. Anything relying on locks (or a `locking;` property) must
   stay behind `#ifdef TESTGITHUB`.
4. **Modules listening to `layer_state_changed` do not see lock changes.** That is the
   point (no double `enter`), but a module written for upstream that expects a second
   event when a layer gets locked would need to listen to `zmk_layer_lock_changed`.

## 6. Files

**New (3):** `app/include/zmk/events/layer_lock_changed.h`,
`app/src/events/layer_lock_changed.c`, this doc.
**Modified (11):** `app/src/keymap.c`, `app/include/zmk/keymap.h`, `app/CMakeLists.txt`,
`app/src/behaviors/behavior_toggle_layer.c`, `behavior_to_layer.c`,
`behavior_momentary_layer.c`, `app/dts/bindings/behaviors/zmk,behavior-toggle-layer.yaml`,
`zmk,behavior-to-layer.yaml`, `zmk,behavior-momentary-layer.yaml`,
`app/dts/behaviors/toggle_layer.dtsi`, `to_layer.dtsi`.
**Tests (new):** `app/tests/layer-locking/` (5 cases).

## 7. Verification

ZMK's test harness (`native_posix_64`, simulated time). New suite `tests/layer-locking/`,
snapshots reviewed line by line:

| case | checks |
|---|---|
| 1 tog-while-mo-held-locks | `&mo 1` held, `&tog 1` inside the layer → lock event only (no layer change); `&mo` release ignored; `&tog 1` again → off + unlock |
| 2 tog-from-base-locks | `&tog 1` → on + lock; `&mo 1` (through `&trans`) press/release ignored; `&tog 1` → off |
| 3 to-locks | `&to 1` → on + lock; `&mo 1` release ignored; `&to 0` → layer 1 off + unlocked |
| 4 opt-out-tog | `&tog_nl` (no `locking;`) flips on/off like 24.12; a `&mo` release turns off the unlocked layer; after a stock `&tog` locks it, `&tog_nl` is ignored; the stock `&tog` turns it off |
| 5 mo-release-on-unlocked | plain `&mo` unchanged |

The idle-lock suite (17 cases, `PATCH_LAYER_IDLE_LOCK.md` §8) runs on top of this patch.
Full suite: 265 of 266 pass, the other is upstream's `pending` case; the stock
`toggle-layer`, `to-layer`, `momentary-layer`, `sticky-keys`, `conditional-layer` and
`hold-tap` suites pass with their snapshots unchanged. `go60_lh` / `go60_rh` build with no
new warnings (details in `PATCH_LAYER_IDLE_LOCK.md` §8).
