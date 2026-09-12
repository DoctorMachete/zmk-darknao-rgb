# PATCH — `&magic_indicator_*` (relocatable BLE/USB indicators)

**Status: working on hardware (LH).** Shows the native Magic-layer BLE/USB
connection indicators at arbitrary keymap positions, updating **live** while the
layer is held.

Shared background: `PATCH_ARCHITECTURE.md`. Depends on the `&pixel` split channel:
`PATCH_PIXEL.md`.

---

## 1. What it does

Paints the 4 BLE profile dots + 1 USB dot wherever you want them, on either half,
instead of only at their fixed native positions. The layer listener shows them on
enter and hides them on exit; colours track connection state in real time while the
layer is held.

## 2. Key architectural decision — push a resolved colour, not a "BLE op"

The peripheral **cannot** compute BLE/USB colour: endpoint and profile state live
on the central only. So the central resolves the colour and pushes **finished RGB**
through the existing `&pixel` `OP_SET` split path.

Consequences: **no new split opcode, zero peripheral-side logic.** This is why the
feature is small.

> **Do not "fix" this back onto the native `persist_usb` / `persist_ble` peripheral
> path.** That reintroduces a documented no-op.

## 3. Live updating

The layer listener only fires at enter/exit, not while held. So a central-side
registry (**max 8 entries**) tracks `{position, which}`, and a dedicated
`ZMK_LISTENER` in `magic_indicator.c` subscribes to:

- `ble_active_profile_changed`
- `endpoint_changed`
- `usb_conn_state_changed`

…and repaints every registered indicator on any change.

**Cost:** each remote-half repaint is a split BLE write. Trivial at 5 indicators;
only worth watching if scaled to many remote dots during heavy churn.

## 4. Colour ladder (mirrors native exactly, packed `0xRRGGBB`)

| state | colour |
|---|---|
| BLE profile *i*: connected **and** active endpoint is BLE **and** active == *i* | **white** |
| BLE profile *i*: connected | **dull green `0x00ff68`** |
| BLE profile *i*: bonded, not connected | **red `0xff0000`** |
| BLE profile *i*: unbonded | **lilac `0x6b1fce`** |
| USB: HID ready + active endpoint | white |
| USB: HID ready | dull green |
| USB: powered only | red |
| USB: none | lilac |

Both ladders are factored into shared helpers `zmk_magic_indicator_ble_color(i)` /
`_usb_color()` that **both** the native render path and the relocated path call —
so the moved dot can never drift from the native one. Keep it that way.

## 5. Behaviors

| behavior | cells | purpose |
|---|---|---|
| `&magic_indicator_on POS WHICH` | `<2>` | register + paint one indicator |
| `&magic_indicator_off POS WHICH` | `<2>` | deregister + clear that pixel |
| `&magic_indicator_clear` | `<0>` | clear **all** active indicators (both halves) |
| `&magic_indicator_group` | `<0>` | paint a fixed set in ONE queue entry |

All `BEHAVIOR_LOCALITY_GLOBAL`. `POS` is a **global** position (same space as
`&pixel`).

**`&magic_indicator_group`** is configured by two DT array properties instead of
binding params:
- `positions` — global pixel positions
- `selection` — matching WHICH values
- **The two arrays must be the same length.**

### WHICH constants
`app/include/dt-bindings/zmk/magic-indicator.h`:
- `MAGIC_BLE0..MAGIC_BLE4` = profile index `0..4`
- `MAGIC_USB` = **64** (deliberately high so param2 unambiguously selects USB vs a
  BLE profile)

## 6. Compositing — the two-layer model

MAGIC indicators get their **own** buffer layer (`magic_pixels[]` /
`magic_pixel_active[]` / `any_magic_pixel`), composited **above** the `&pixel`
layer.

This fixed a collision where an indicator and a `&pixel` override at the same LED
both wrote the single `pixel_overrides[]` cell (last-writer-wins), so toggling one
wiped the other. Now clearing the magic layer **reveals** the `&pixel` pixel beneath
instead of leaving the LED dark.

**Resulting priority (low → high):**
`underglow → &pixel overrides → PIXLBLINK → MAGIC indicators`

Implementation notes:
- `generate_status()` paints the magic layer **after** (above) `pixel_overrides[]`.
- Overlay gates include `any_magic_pixel` so a lone indicator still composites.
- `clear_pixels()` also wipes the magic layer — *3 lines, removable if `&pixel`
  `CLEAR_ALL` should leave indicators intact.*
- `magic_indicator.c` writes via `set_magic_pixel`, not `set_pixel`.
- **`any_magic_pixel` must be in the ext-power gate** (`PATCH_ARCHITECTURE.md` §4) —
  omitting it was the original "indicators invisible" bug.
- Uses the file's GNU colon-form designated initialisers (`(struct led_rgb){r : …}`)
  — do not rewrite to `.r =`.

### ⚠️ Split layer-id caveat — keep indicators LH-only
The two-layer model lives in the **central driver only**. The split channel carries
a colour, not a layer id, so an RH indicator colliding with an RH `&pixel` at the
same LED is still last-writer-wins on the peripheral.

**Keep MAGIC indicators on the left half** until the protocol distinguishes layers.
All current indicators are LH, so this is unreached.

## 7. Enabling & files

**Kconfig:** `CONFIG_ZMK_MAGIC_INDICATOR=y` in `go60.conf` (symbol default is `n`).
The whole feature is behind this gate.

**New files:** `app/src/magic_indicator.c`, `app/include/zmk/magic_indicator.h`,
`app/include/dt-bindings/zmk/magic-indicator.h`,
`app/src/behaviors/behavior_magic_indicator_{on,off,clear,group}.c`, and their four
binding YAMLs.

**Modified:** `app/src/rgb_underglow.c` (+ `.h`), `app/Kconfig`,
`app/CMakeLists.txt`.

## 8. Keymap integration

1. `CONFIG_ZMK_MAGIC_INDICATOR=y` in `go60.conf`.
2. `#include <dt-bindings/zmk/magic-indicator.h>` next to the `go60-pixels.h` include.
3. Declare the behaviors you use inside the top-level `behaviors { }` block (inside
   `#ifdef TESTGITHUB`). A group node, as currently used:

```dts
magic_status_ble: magic_status_ble {
    compatible = "zmk,behavior-magic-indicator-group";
    #binding-cells = <0>;
    positions  = <PIXEL_LH_C2R5 PIXEL_LH_C3R5 PIXEL_LH_C4R5 PIXEL_LH_C5R4 PIXEL_LH_C6R4>;
    selection  = <MAGIC_BLE0    MAGIC_BLE1    MAGIC_BLE2    MAGIC_BLE3    MAGIC_USB>;
};
```

4. Add a listener in `layer_listeners { }`:

```dts
bleusb_NAV {
    layers = <LAYER__NAV>;
    not-press-release-outputs;
    enter = <&magic_status_ble>;
    exit  = <&magic_indicator_clear>;
};
```

> **`&magic_indicator_clear` must be declared as a node** (it lives in
> `config/darknaofork_patches.dtsi`) or its driver compiles out and the label is
> undefined — see `PATCH_ARCHITECTURE.md` §7.

## 9. Failure triage

| symptom | first suspect |
|---|---|
| indicators do nothing at all | `CONFIG_ZMK_MAGIC_INDICATOR` not set, or the behavior nodes not declared |
| invisible only when RGB underglow is off | `any_magic_pixel` missing from the ext-power gate |
| colours differ from the native Magic layer | someone duplicated the ladder instead of calling the shared helpers |
| indicator and a `&pixel` fight over one LED on the RH | the LH-only caveat (§6) |
| dots don't update while the layer is held | the event `ZMK_LISTENER` in `magic_indicator.c` |

## 10. Open items
- [ ] PIXLBLINK vs MAGIC priority at a shared LED — currently MAGIC wins. Confirm.
- [ ] Whether `&pixel` `CLEAR_ALL` should leave indicators intact (the 3 lines in
      `clear_pixels()`).
- [ ] RH indicators remain blocked by the split layer-id caveat.
