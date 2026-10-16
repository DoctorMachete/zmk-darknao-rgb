# PATCH — `&kt_iflayer` (key toggle, only while a layer is active)

**Status: implemented and tested off-hardware, not yet flashed.** 7 native_posix cases,
upstream's `keytoggle` cases, the full suite, both GO60 target builds (§7).

A two-parameter variant of `&kt`: `&kt_iflayer KEY LAYER` toggles `KEY` exactly as `&kt`
would, but only if `LAYER` is active when it's pressed. Otherwise it does nothing.

Shared background: `PATCH_ARCHITECTURE.md`. Layer semantics: `PATCH_LAYER_LOCKING.md`.

---

## 1. What it does

```dts
&kt_iflayer LSHFT LAYER__MOUSE     /* toggles Shift, but only while MOUSE is on */
```

| on press | result |
|---|---|
| `LAYER` active | toggle `KEY`, same as `&kt KEY` (with `toggle-mode`, §2) |
| `LAYER` not active | nothing; the key is consumed |
| `LAYER` is not a layer of the keymap | nothing, plus a log warning |

- **"Active"** is `zmk_keymap_layer_active()`: on by any means (`&mo`, `&tog`, `&to`, a
  conditional layer, the trackpad's temp layer, the idle lock), locked or not. The
  default layer always counts as active, so `&kt_iflayer KEY 0` is just `&kt KEY`.
- **Checked at press time.** Release does nothing, as with `&kt`.
- **Where the binding sits doesn't matter.** On the base layer under a `&trans` (the
  tests do that), inside the layer itself, in a macro: the check is always against
  `LAYER`, not against the layer the binding came from.
- Parameter order: key first, then layer, as in the name.

## 2. `toggle-mode`

Same property, values and default as `&kt` (`zmk,behavior-key-toggle`):

| `toggle-mode` | while `LAYER` is active |
|---|---|
| `"flip"` (default, the shipped `&kt_iflayer`) | pressed → release, released → press |
| `"on"` | press (again, if already pressed: the HID layer re-sends it, as `&kt` does) |
| `"off"` | release |

For the other modes, define your own instance:

```dts
kt_iflayer_off: kt_iflayer_off {
    compatible = "zmk,behavior-key-toggle-if-layer";
    #binding-cells = <2>;
    toggle-mode = "off";
};
```

## 3. ⚠️ The layer can go away while the key is held

`&kt_iflayer` toggles a key on while `LAYER` is active. If `LAYER` then turns off (the
`&mo` key released, the trackpad's temp layer timing out), the key **stays pressed**,
and `&kt_iflayer KEY LAYER` can no longer release it, because the layer is off. Test 6
pins this.

Ways out, all with existing pieces:
- a plain `&kt KEY` (or a `toggle-mode = "off"` `&kt` instance) anywhere reachable;
- release it automatically when the layer goes: a layer listener on `LAYER` with
  `exit = <&kt_release KEY>`, where `kt_release` is a `zmk,behavior-key-toggle` instance
  with `toggle-mode = "off"`. Releasing a key that isn't pressed does nothing, so this is
  safe to fire every time.

If that should be built in (say, "release always works, only *pressing* needs the
layer"), it's a small option to add; not done until it's wanted.

## 4. Why the fork and not a module

It needs only public ZMK API (`zmk_keymap_layer_active()`, the keycode event), so a module
would work. The fork is still the better home here:
- **No new pin.** Modules in `west.yml` are branch-tracked, and module drift is already
  the first suspect when a good build breaks. The fork is SHA-pinned and its SHA bump is
  the cache invalidation (`PATCH_ARCHITECTURE.md` §1.1).
- **Tests run in place.** The fork's `app/tests` harness takes a new folder; a module
  would need `ZMK_EXTRA_MODULES` wiring to test.
- **One small, self-contained file** next to `&kt`, which it mirrors: little extra
  rebase cost on a fork that already diverges a lot.

The case for a module would be sharing it with other people, or leaving the fork for
upstream ZMK; then this file moves out unchanged.

## 5. Firmware

| file | what |
|---|---|
| `app/src/behaviors/behavior_key_toggle_if_layer.c` | the driver: layer check, then `&kt`'s toggle logic; Studio parameter metadata (key, layer) |
| `app/dts/bindings/behaviors/zmk,behavior-key-toggle-if-layer.yaml` | `two_param.yaml` + `toggle-mode` |
| `app/dts/behaviors/key_toggle_if_layer.dtsi` | the shipped `kt_iflayer: key_toggle_if_layer` node, `/omit-if-no-ref/` like stock `&kt` (omit switch `KT_IFLAYER`) |
| `app/dts/behaviors.dtsi` | includes it, after `key_toggle.dtsi` |
| `app/Kconfig.behaviors` | `ZMK_BEHAVIOR_KEY_TOGGLE_IF_LAYER`, default y when a node exists |
| `app/CMakeLists.txt` | the source, central-only, next to `&kt` |

- A separate compatible because a binding's cell count is fixed per compatible (`&kt`
  is `one_param.yaml`).
- **Central only**, like `&kt` (behavior locality default): it never crosses the split.
  No event subscriptions, so link order (constraint #11) doesn't apply.
- Unreferenced, the node is omitted and the driver compiles out: a keymap that doesn't
  use it gets nothing new.
- `param2` is a layer id, as for `&mo`; anything `≥ ZMK_KEYMAP_LAYERS_LEN` is refused
  before `zmk_keymap_layer_active()` (whose `BIT(layer)` would be undefined past 31).

## 6. Keymap wiring (GO60)

`&kt_iflayer` is fork-only (constraint #6): MoErgo's firmware doesn't have it. Use it
inside the `TESTGITHUB` region (fork-only macros, listeners, adaptive sequences), or give
a layer-grid binding the usual `#ifndef` fallback. A fallback must take the same two
cells; for example a two-parameter macro that toggles unconditionally (or does nothing,
with `&none` instead of `&kt`):

```dts
/* inside the TESTGITHUB region of behaviors { } */
#define KT_IFLAYER kt_iflayer

/* outside that region, still inside a behaviors { } block, next to the other
   #ifndef fallbacks (the node needs a behaviors { } parent, constraint #5) */
#ifndef KT_IFLAYER
kt_iflayer_moergo: kt_iflayer_moergo {
    compatible = "zmk,behavior-macro-two-param";
    #binding-cells = <2>;
    tap-ms = <0>;
    wait-ms = <0>;
    bindings = <&macro_param_1to1>, <&kt MACRO_PLACEHOLDER>;   /* layer (param2) ignored */
};
#define KT_IFLAYER kt_iflayer_moergo
#endif

/* grid: &KT_IFLAYER LSHFT LAYER__MOUSE */
```

`MACRO_PLACEHOLDER` is required (constraint #15). Not built against MoErgo's firmware here.

## 7. Tests and verification

`app/tests/key-toggle-if-layer/` — base `&kt_iflayer B 1` under a `&trans`, `&mo 1`,
`&tog 1`, `&kt_iflayer C 0`, `&kt_iflayer D 9`, a plain `&kt B`, and `"on"` / `"off"`
instances on layer 1:

| case | checks |
|---|---|
| `1-layer-off-does-nothing` | layer off: two taps, nothing sent |
| `2-mo-layer-toggles` | under `&mo`: on, off |
| `3-tog-layer-toggles` | under a locking `&tog`: on, off; layer off again: nothing |
| `4-default-layer-always-active` | layer 0: behaves as `&kt` |
| `5-toggle-modes` | `"on"` twice (re-sent), then `"off"` releases |
| `6-layer-gone-key-stays` | pins §3: the layer drops, the key stays, `&kt_iflayer` can't release it, `&kt` can |
| `7-unknown-layer-ignored` | layer 9 of 2: warning, nothing sent |

- Snapshots reviewed line by line. Mutation check: with the layer condition removed,
  cases 1 and 6 fail.
- Upstream `app/tests/keytoggle/` (6 cases): pass.
- Full suite (`run-test.sh all`): 280 pass, 1 pending (upstream's own `pending` case), no
  failures; 281 cases, the 274 from before plus these 7.
- `go60_lh` / `go60_rh` (stock GO60 keymap, `&kt_iflayer LSHFT LAYER_Keypad` on a thumb):
  clean, no new warnings; the driver is in the LH image.
- **Not done:** hardware.
