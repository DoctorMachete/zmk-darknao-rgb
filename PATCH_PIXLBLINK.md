# PATCH — `&pixlblink` presets + `&pixlblink_off`

**Status: working on hardware, both halves.** Sharp square-wave blink of one pixel
between two colours, at a per-preset frequency.

Shared background: `PATCH_ARCHITECTURE.md`. Sibling layers: `PATCH_PIXEL.md`,
`PATCH_MAGIC_INDICATOR.md`.

> **Spelling:** `PIXLBLINK` has no `E`, deliberately, so searches never collide with
> `PIXEL` / `&pixel`.

---

## 1. The model

Reusable **presets** defined in the keymap — each carrying its own two colours and
frequency — dropped onto pixels by position:

```dts
blink_recording_macro: blink_recording_macro {
    compatible = "zmk,behavior-pixlblink";
    #binding-cells = <1>;
    frequency = <5>;            /* freq_hz = code / 10  ->  0.5 Hz */
    color1 = <PX_ORANGE>;
    color2 = <PX_RED>;          /* optional; defaults to black */
};
/* ... &blink_recording_macro PIXEL_LH_C3R2   &blink_recording_macro PIXEL_LH_C4R2 ... */
```

stopped by a fixed companion:

```dts
&pixlblink_off PIXEL_LH_C3R2
```

`&pixel` and the MAGIC layer are untouched by this feature.

> **Historic:** an earlier 2-cell form `&pixlblink POS COLOR` (single colour ↔ black,
> one hardcoded rate) was **removed**. Migration: `&pixlblink POS COLOR` → define a
> preset and use `&<preset> POS`; `&pixlblink POS (-1)` → `&pixlblink_off POS`.

## 2. The two behaviors

### 2.1 `zmk,behavior-pixlblink` — preset (user-defined, multi-instance)
- `#binding-cells = <1>`; `param1` = **global** position (`0..29` LH, `30..59` RH).
- DT properties, all optional:

  | property | default | meaning |
  |---|---|---|
  | `color1` | `0xFFFFFF` | ON half-cycle colour |
  | `color2` | `0x000000` | OFF half-cycle colour (black = classic blink) |
  | `frequency` | `5` | freq code; **`freq_hz = frequency / 10`** |

- Multi-instance via `DT_INST_FOREACH_STATUS_OKAY(PB_INST)` with a per-instance
  `behavior_pixlblink_config`. The handler resolves the invoked instance with
  `zmk_behavior_get_binding(binding->behavior_dev)->config` — the same mechanism the
  fork's `sticky_key` uses.
- `BEHAVIOR_LOCALITY_GLOBAL`.
- **The driver compiles in only once at least one preset instance exists** in the
  merged devicetree (the keymap counts). No presets → driver compiles out.

### 2.2 `zmk,behavior-pixlblink-off` — fixed off (NOT user-configurable)
- `#binding-cells = <1>`; `param1` = global position.
- **One instance shipped by the fork** in `app/dts/behaviors/pixlblink.dtsi` as
  `&pixlblink_off`. **Do not redeclare it in the keymap.**
- Stops the blink there, revealing whatever is beneath (a `&pixel` override or the
  underglow). `BEHAVIOR_LOCALITY_GLOBAL`.

### Frequency reference
`freq_hz = frequency / 10`, `half_ms = 5000 / frequency`.

| code | Hz | half-period |
|---|---|---|
| 5 | 0.5 | 1000 ms |
| 10 | 1 | 500 ms |
| 20 | 2 | 250 ms |

## 3. Firmware model (`app/src/rgb_underglow.c`)

```c
struct pixlblink_slot {
    struct led_rgb color1;   // ON half-cycle
    struct led_rgb color2;   // OFF half-cycle (may be black)
    uint32_t half_ms;        // 5000 / freq_code
    bool active;
};
static struct pixlblink_slot pixlblink_slots[STRIP_NUM_PIXELS];
static bool any_pixlblink;
```

- **Per-slot frequency** — because each slot carries its own `half_ms`, different
  pixels blink at different rates simultaneously.
- **Stateless phase**, read per frame from the free-running clock; no counters:
  ```c
  bool on = ((k_uptime_get() / (int64_t)slot.half_ms) & 1) == 0;
  status_pixels[i] = on ? slot.color1 : slot.color2;
  status_overlay_active[i] = true;   // set on BOTH phases
  ```
  The mask is set on both phases because `color2` may be a real colour, not black.
- `pixlblink_half_ms(code)` guards a zero/missing code with
  `PIXLBLINK_DEFAULT_FREQ_CODE` (5) and a `hm == 0 ? 1` floor.
- Colours are brightness-scaled by `CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX`, identical to
  `&pixel`, so blink brightness matches other pixels.
- Composited **between** `&pixel` and MAGIC — a live BLE/USB indicator still wins
  over a decorative blink.
- Uses the file's GNU colon-form designated initialisers.

**API:**
```c
int zmk_rgb_underglow_set_pixlblink(uint32_t pos, uint32_t color1,
                                    uint32_t color2, uint8_t freq_code);
int zmk_rgb_underglow_clear_pixlblink(uint32_t pos);
```

### The three gates — all required
`any_pixlblink` must appear in **all three** (see `PATCH_ARCHITECTURE.md` §4):
1. compositing gates in `zmk_led_write_pixels()`;
2. `desired_state` in `zmk_rgb_set_ext_power()` — *else invisible when underglow is off*;
3. the `persist` condition in `zmk_rgb_underglow_status_update()` — *else it paints
   one frame and freezes*.

The setter also powers the strip, paints immediately, and calls
`k_timer_start(&underglow_status_update_timer, K_NO_WAIT, K_MSEC(25))` so a blink
starts even with underglow off.

## 4. Split

The peripheral runs its own 25 ms tick and **animates autonomously** — the central
pushes the spec once, never frames.

- Ops `ZMK_SPLIT_RGB_PIXEL_OP_PIXLBLINK = 7` / `..._PIXLBLINK_CLEAR = 8`.
- Senders `zmk_split_central_set_pixlblink(pos, color1, color2, freq_code)` and
  `zmk_split_central_clear_pixlblink(pos)`.
- Both peripheral dispatchers handle the ops: `bluetooth/service.c` (live BLE path)
  and `peripheral.c` (generic/wired — carried but untested on hardware).
- The blink's `color2` + `freq_code` ride in the payload **union**, sharing storage
  with the battery op's `positions[]`. This keeps the payload at **16 bytes**.

> ⚠️ **This union is not a style choice — it is the fix for a real bug.** Appending
> `color2`/`freq_code` as flat fields made the payload 24 bytes, past the
> **20-byte ATT write-without-response ceiling**, and every RGB pixel write to the
> right half was rejected before transmission — killing `&pixel` too. See
> `PATCH_ARCHITECTURE.md` §5.1 before touching this struct.

## 5. Semantics

- `&<preset> POS` — start (or replace) the blink at POS.
- `&pixlblink_off POS` — stop it; other blinking pixels continue.
- `color2` omitted / `0x000000` → classic colour↔off blink.
- `color1 == color2` → solid colour, not an error.
- `PIXEL_CLEAR_*` sentinels are **not** guaranteed to stop blinks; only
  `&pixlblink_off` is.
- **L/R phase drift is accepted.** The halves run independent clocks, so a
  long-lived blink slowly drifts out of step between sides. Fine for a status
  blink; strict sync would need a pushed phase epoch — and that costs payload
  bytes (§4).

## 6. Files

**New (5):** `app/src/behaviors/behavior_pixlblink.c`,
`app/src/behaviors/behavior_pixlblink_off.c`, `app/dts/behaviors/pixlblink.dtsi`,
`app/dts/bindings/behaviors/zmk,behavior-pixlblink.yaml`,
`app/dts/bindings/behaviors/zmk,behavior-pixlblink-off.yaml`.

**Modified (9):** `app/src/rgb_underglow.c` (+ `.h`),
`app/include/zmk/split/transport/types.h`, `app/include/zmk/split/central.h`,
`app/src/split/central.c`, `app/src/split/bluetooth/service.c`,
`app/src/split/peripheral.c`, `app/CMakeLists.txt`, `app/dts/behaviors.dtsi`.

## 7. Keymap usage

**Presets go in the top-level `behaviors { }` block** (with `magic_status_ble`),
inside `#ifdef TESTGITHUB`. **Never inside `layer_listeners { }`** — the listeners
module compiles every child of that node as a listener and the build dies in
`LAYER_LISTENER_INST`.

```dts
    blink_a: blink_a { compatible = "zmk,behavior-pixlblink"; #binding-cells = <1>;
                   frequency = <10>; color1 = <PX_ORANGE>; color2 = <PX_RED>; };   /* 1 Hz */

    blink_b: blink_b { compatible = "zmk,behavior-pixlblink"; #binding-cells = <1>;
                   frequency = <20>; color1 = <PX_GREEN>; };                        /* 2 Hz, green<->off */
```

Listener in `layer_listeners { }` — every position started must be cleared:

```dts
    blink_test {
        layers = <LAYER__MEDIA>;
        not-press-release-outputs;
        enter = <&blink_a       PIXEL_LH_C2R2   &blink_b       PIXEL_RH_C2R2>;
        exit  = <&pixlblink_off PIXEL_LH_C2R2   &pixlblink_off PIXEL_RH_C2R2>;
    };
```

Or on keys: `&blink_a PIXEL_LH_C6R4` to start, `&pixlblink_off PIXEL_LH_C6R4` to
stop (there is no "release stops it").

**Reminders:** `PX_*` or raw `0xRRGGBB` only; `POS_*` are not strip indices;
frequency lives on the preset, not the invocation; presets are cheap — define one
per colour/rate combination.

## 8. Bench test

1. Define `blink_a` (0.5 Hz, orange↔red) and `blink_b` (1 Hz, green↔off).
2. Listener: `enter = <&blink_a PIXEL_LH_C3R2 &blink_b PIXEL_RH_C3R2>;`,
   `exit = <&pixlblink_off PIXEL_LH_C3R2 &pixlblink_off PIXEL_RH_C3R2>;`
3. Expect LH orange↔red at ~1 s per phase; RH green↔off at ~0.5 s per phase —
   **different rates** confirms per-preset frequency.
4. With `color2` set, the OFF half is that colour, not forced black.
5. **Underglow OFF** → must still blink (gates 2 and 3).
6. Leaving the layer stops both and reveals what's beneath.

## 9. Failure triage

| symptom | first suspect |
|---|---|
| RH dead for **all** pixel ops (`&pixel` too) while keys and layer RGB work | payload > 20 bytes (`PATCH_ARCHITECTURE.md` §5.1) |
| invisible only when underglow is off | `any_pixlblink` missing from the ext-power gate |
| one frame then frozen | refresh timer not kept alive |
| `&pixlblink`/`&<preset>` undefined at DT parse | no preset instance declared |
| build dies in `LAYER_LISTENER_INST` | presets placed in `layer_listeners { }` |
| wrong RH pixel blinks | `PIXEL_RH_*` value in `go60-pixels.h` |

## 10. Open items
- [ ] PIXLBLINK vs MAGIC priority at a shared LED (currently MAGIC wins) — confirm.
- [ ] Should `PIXEL_CLEAR_*` also stop blinks?
- [ ] L/R phase sync — currently accepted drift.
- [ ] Move `PIXLBLINK_DEFAULT_FREQ_CODE` to `go60.conf` as
      `CONFIG_ZMK_PIXLBLINK_FREQ_CODE`?
- [ ] `peripheral.c` (wired/generic path) carries the ops but is untested.
