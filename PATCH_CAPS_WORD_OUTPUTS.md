# PATCH — `caps_word` `enter` / `exit` outputs

**Status: implemented in the fork, not yet built or flashed.** Adds `enter` / `exit`
behavior chains to `zmk,behavior-caps-word`, mirroring the `zmk-listeners` API, so
caps-word activation can drive `&pixlblink` — and later a buzzer, display, or haptic,
anything that is a behavior.

Shared background: `PATCH_ARCHITECTURE.md`. Consumer of `PATCH_PIXLBLINK.md`.

---

## 1. Why this shape

Caps word has no event and no listener hook in ZMK — its state lives entirely in
`behavior_caps_word_data.active`, mutated in exactly two places. Three approaches
were considered:

| approach | verdict |
|---|---|
| **`enter` / `exit` on the node** | **chosen.** One repo, one build, no HID traffic, fires exactly at the state edges, reuses a DT idiom the keymap already uses five times. |
| dead key + `keycode_listeners` | rejected here — §1.1. |
| new `zmk_caps_word_state_changed` event + a `capsword-listeners` type in `zmk-listeners` | deferred — §6. Strictly better *if* a non-behavior consumer (status screen) ever appears. |

### 1.1 Why not the dead key

The dead-key trick works well elsewhere in this keymap, but caps word is the one
place it fights itself.

`caps_word_keycode_state_changed_listener()` deactivates on any press that is not
alpha, not numeric, not a modifier, and not in `continue-list`. A dead key raised at
activation is exactly such a press, so caps word would switch itself off in the same
event chain that turned it on.

This is not hypothetical — checked against the keymap's actual dead keys:

```c
#define DEADKEY_I    C_CHAN_INC     /* consumer page, id 0x9C */
#define FONTSTYLE    C_CHAN_DEC
#define FONTCAPTURE  C_CHAN_LAST
```

`caps_word_is_alpha()` tests the usage **id** against `0x04..0x1D` without looking at
the usage page; `caps_word_is_numeric()` likewise; `is_mod()` is false. `0x9C` lands
outside both ranges, falls through, and calls `deactivate_caps_word()`.

Workable by adding the dead key to every instance's `continue-list`, but that is a
silent landmine for whoever edits that list next. It is also a real key on the wire:
a slot in the boot report, subject to host auto-repeat, visible to remappers, held for
the whole run — for a signal that never needs to leave the keyboard.

> **Related sharp edge, worth remembering independently:** because those range checks
> ignore the usage page, a consumer or media keycode whose **id** happens to fall in
> `0x04..0x1D` will silently *continue* caps word rather than break it. None of the
> three dead keys above do.

## 2. The model

```dts
caps_word {
    continue-list = < ... >;
    not-press-release-outputs;
    enter = <&blink_capsword PIXEL_LH_C1R3>;
    exit  = <&pixlblink_off  PIXEL_LH_C1R3>;
};
```

- Both properties optional and chainable — several behaviors fire in order, no
  wrapping macro needed. Same semantics as `layer_listeners`.
- `not-press-release-outputs` has the **same name, meaning, and default as in
  `zmk-listeners`**: absent ⇒ queued press-then-release with `tap-ms` / `wait-ms`;
  present ⇒ each binding's press handler invoked directly and synchronously, no
  release. Deliberately kept identical rather than "improved" — five node types in
  this keymap already read this way.
- Per instance; multiple caps-word behaviors each get their own outputs.
- Fires on **state edges only**, never on a repeat of the same state.

## 3. Firmware — `app/src/behaviors/behavior_caps_word.c`

### 3.1 Config struct

`continuations[]` is a flexible array member and **must stay last**. The binding
pointers are non-const because `behavior_keymap_binding_pressed()` takes a non-const
binding; the config object itself stays `const`.

```c
struct behavior_caps_word_config {
    zmk_mod_flags_t mods;
    struct zmk_behavior_binding *enter_bindings;
    struct zmk_behavior_binding *exit_bindings;
    uint8_t enter_bindings_len;
    uint8_t exit_bindings_len;
    bool not_press_release_outputs;
    uint16_t tap_ms;
    uint16_t wait_ms;
    uint8_t continuations_count;
    struct caps_word_continue_item continuations[];
};
```

### 3.2 The state chokepoint

`activate_caps_word()` / `deactivate_caps_word()` became one-line wrappers around
`set_caps_word_state()`, so the rest of the file is untouched. Everything that can
change caps-word state already went through those two functions.

`data->active` is set **before** firing, and the `data->active == active` early return
makes the transition edge-only.

```c
static void set_caps_word_state(const struct device *dev, bool active) {
    struct behavior_caps_word_data *data = dev->data;
    const struct behavior_caps_word_config *config = dev->config;

    if (data->active == active) {
        return;
    }

    data->active = active;

    if (active) {
        caps_word_fire(config, config->enter_bindings, config->enter_bindings_len);
    } else {
        caps_word_fire(config, config->exit_bindings, config->exit_bindings_len);
    }
}
```

### 3.3 `caps_word_fire()`

> **`behavior_keymap_binding_pressed()`, not `zmk_behavior_invoke_binding()`.** The
> latter honours DT locality, and `&pixlblink` is `BEHAVIOR_LOCALITY_GLOBAL`, so it
> would additionally ship a split *invoke-behavior* message to the peripheral — where
> `pixlblink_pressed()` would take the non-central branch and log a warning. The pixel
> behaviors relay themselves from inside the handler (`pos >= local_count` ⇒
> `zmk_split_central_set_pixlblink`). This is the same call `zmk-listeners` makes, for
> the same reason.

The binding event is built the same way the listeners module builds it:
`.position = INT32_MAX`, `.timestamp = k_uptime_get()`, and `.source =
ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL` under `CONFIG_ZMK_SPLIT`.

### 3.4 Binding extraction macros

`ZMK_KEYMAP_EXTRACT_BINDING` is hardcoded to the `bindings` property, so the file
carries prop-parameterised twins — `CW_EXTRACT_BINDING`, `CW_BINDINGS_ARRAY`,
`CW_PROP_LEN`, `CW_BINDINGS_PTR` — built on `DT_INST_*` because `enter` / `exit` sit
on the instance node itself rather than on a child. Same construct as
`zmk-listeners/src/listener_bindings.h`; that module is not reachable from the fork's
app sources (its CMake exports only `include/`, not `src/`), and a fork→module
dependency would be the wrong direction anyway.

Absent property ⇒ no array emitted, pointer `NULL`, length `0`, and `caps_word_fire()`
returns immediately. A fork build with no `enter` / `exit` behaves exactly as before.

### 3.5 Includes added

`<zephyr/kernel.h>` (for `k_uptime_get`) and `<zmk/behavior_queue.h>`.

No change to `app/CMakeLists.txt` — `behavior_caps_word.c` is already unconditionally
compiled (line 54). No change to `app/dts/behaviors/caps_word.dtsi` — the shipped
`&caps_word` node picks the properties up from the keymap.

## 4. DT binding — `app/dts/bindings/behaviors/zmk,behavior-caps-word.yaml`

Adds `enter`, `exit` (both `phandle-array`, `specifier-space: binding`),
`not-press-release-outputs` (boolean), and `tap-ms` / `wait-ms` (int, default 5).
`continue-list` stays required; `mods` unchanged.

## 5. Constraints

### 5.1 Which architecture constraints this touches

- **#1 (20-byte split payload): untouched.** Nothing new crosses the split.
  `&pixlblink` already relays itself; this patch only decides *when* to call it.
- **#4 (node must exist or the driver compiles out):** applies to the new
  `blink_capsword` preset, not to `caps_word`. `&caps_word` is `/omit-if-no-ref/` and
  the keymap references it from a hold-tap, so it survives.
- **#5 (behavior nodes live in `behaviors { }`):** the preset goes in `behaviors { }`,
  never in a listeners node.
- **#6 (`TESTGITHUB`):** see §7 — the keymap side must be guarded, and the guard is
  load-bearing for the MoErgo build.
- **Deploy ritual:** fork source change ⇒ rebuild **and bump the fork SHA in
  `west.yml`**, then reflash both halves.

### 5.2 New hazards

1. **Never bind key output to `enter`.** A binding that raises a keycode feeds straight
   back into the word-break check and switches caps word off. The edge-only guard makes
   this terminate rather than recurse, but the feature still will not work.
   Side-effect-only outputs only. (This is §1.1's failure mode arriving by another door.)
2. **`exit` fires during the breaking keypress**, inside the keycode-event callback,
   before that key reaches the host. Fine for an indicator; worth knowing if a future
   binding is order-sensitive.
3. **`&pixlblink_off` only reveals what is underneath.** It does not clear a `&pixel`
   override at the same position — which §7 turns into a feature.

## 6. Optional follow-up: the event

If a consumer appears that is not a behavior — a status-screen widget, a WPM-style
display element — promote the chokepoint to an event rather than growing this property
list:

```c
/* in set_caps_word_state(), after data->active = active; */
#if IS_ENABLED(CONFIG_ZMK_CAPS_WORD_STATE_EVENT)
    raise_zmk_caps_word_state_changed((struct zmk_caps_word_state_changed){
        .dev = dev, .active = active, .timestamp = k_uptime_get()});
#endif
```

plus `app/include/zmk/events/caps_word_state_changed.h`,
`app/src/events/caps_word_state_changed.c`, and a `target_sources_ifdef` line beside
the other events in `app/CMakeLists.txt` (lines 38–122). A `zmk,capsword-listeners`
node type in `zmk-listeners` would then consume it with `listener_bindings.h`
unmodified — gated on the Kconfig symbol, which only exists in this fork, so the module
still builds against stock ZMK.

Don't build this until there is a second consumer. `enter` / `exit` already covers the
buzzer case; a buzzer is a behavior.

## 7. Keymap wiring

The keymap's existing `caps_word` node (inside `/ { behaviors { } }`, currently
unguarded) is the right place — one node, `continue-list` next to the indicator wiring.
Do **not** add a separate `&caps_word { }` override block.

The indicator half must be guarded so the MoErgo web editor still compiles: `enter` on
an undeclared property is only an edtlib *warning* there, but `&blink_capsword` is an
unresolved label, which is a hard error. So the guard has to cover the preset node too.

```dts
/* in the behaviors { } block, inside the existing #ifdef TESTGITHUB region */
blink_capsword: blink_capsword {
    compatible = "zmk,behavior-pixlblink";
    #binding-cells = <1>;
    frequency = <10>;          /* 1 Hz */
    color1 = <PX_YELLOW>;
};

/* the existing node */
caps_word {
    continue-list = <
      UNDERSCORE MINUS LS(MINUS)
      BACKSPACE DELETE
      N1 N2 N3 N4 N5 N6 N7 N8 N9 N0 SQT LS(SQT) LS(N9) LS(N0) FSLH BSLH
      LS(COMMA) LS(SEMI) LS(DOT) LS(BSLH)
    >;
    #ifdef TESTGITHUB
      not-press-release-outputs;
      enter = <&blink_capsword PIXEL_LH_C1R3>;
      exit  = <&pixlblink_off  PIXEL_LH_C1R3>;
    #endif
};
```

> **One untested thing.** DT sources go through the C preprocessor, so a directive
> between properties is valid and compiles. But every one of the ~147 directives in
> `go60.keymap` today sits at brace depth ≤ 2 — they wrap whole nodes or `#define`s,
> never properties inside a node body. This would be the first in-node directive in the
> repo, so whether the MoErgo editor preserves it across a save/reopen round-trip is
> unverified. Cheapest check: add the guard with an empty body, save, reopen.

### Pixel choice

- **Prefer a left-half pixel.** Caps word runs on the central, so LH is a local call
  with no split write. RH works (the dynamic-macro listener drives RH pixels all day) at
  the cost of one relayed write per edge.
- **Avoid the MAGIC pixels.** MAGIC outranks PIXLBLINK in the composite, so a caps-word
  blink parked under a BLE/USB dot is invisible while `bleusb_NAV` is engaged.
- **Stacking with caps *lock* is worth considering.** `caps_feedback` in `hid_listeners`
  already drives a `&pixel` override for the caps-lock LED. PIXLBLINK composites *above*
  `&pixel`, and `&pixlblink_off` reveals whatever is beneath — so putting caps word on
  the same pixel gives, for free: solid = caps lock, blinking = caps word, and the solid
  reappearing if both were on. One LED, two states, correct on every transition order.

## 8. Files

**Modified (2):** `app/src/behaviors/behavior_caps_word.c`,
`app/dts/bindings/behaviors/zmk,behavior-caps-word.yaml`.
**New (1):** this doc. **Deleted:** none.
Keymap gains one preset node and three guarded lines.

## 9. Verification done off-hardware

- `caps_word_fire()` / `set_caps_word_state()` / the wrappers extracted into a stub
  harness and compiled with `gcc -Wall -Wextra`: clean. Edge-only behaviour exercised
  for double-activate, double-deactivate, and the full toggle cycle.
- The four `CW_*` DT macros expanded through `cpp` against stubbed `COND_CODE_*` /
  `LISTIFY` / `DT_INST_*`, for one node with a 2-entry `enter` (first entry carrying a
  `param1`) and no `exit`. Present ⇒ correct 2-element array, pointer to it, length 2.
  Absent ⇒ nothing emitted, `NULL`, length 0.
- Binding YAML parses; brace/paren/bracket balance and macro line-continuations checked
  across the patched `.c`.

**A Zephyr build cannot be run here** (working practice §9) — CI plus a flash is still
the real test.
