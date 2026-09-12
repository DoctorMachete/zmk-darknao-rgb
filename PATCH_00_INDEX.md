# GO60 fork patches — INDEX

Customisations applied to `zmk-darknao-rgb` (darknao/zmk, branch `rgb-layer-24.12`,
via the DoctorMachete mirror). **Start here.**

## Documentation map

| doc | covers |
|---|---|
| **`PATCH_ARCHITECTURE.md`** | Shared foundations: the RGB composite stack, split transport, addressing, build/deploy, and the **hard constraints**. Read before touching any feature. |
| **`PATCH_PIXEL.md`** | `&pixel` — static per-pixel RGB override on either half. The foundation the other RGB features build on. |
| **`PATCH_MAGIC_INDICATOR.md`** | `&magic_indicator_*` — relocatable, live-updating BLE/USB status indicators. |
| **`PATCH_PIXLBLINK.md`** | `&pixlblink` presets + `&pixlblink_off` — sharp two-colour blink at a per-preset frequency. |
| **`PATCH_CAPS_WORD_OUTPUTS.md`** | `enter` / `exit` behavior chains on `zmk,behavior-caps-word` — drives an indicator (PIXLBLINK today) from caps-word activation. |
| `PATCH_PIXEL_REFERENCE_go60-pixels.h` | Reference copy of the pixel-address header. **Nothing includes it** — the live header is `app/include/dt-bindings/zmk/go60-pixels.h`. |

## Feature status

| feature | state |
|---|---|
| `&pixel` | **Working**, both halves |
| MAGIC indicators | **Working**, LH (see LH-only caveat in its doc) |
| PIXLBLINK presets + off | **Working**, both halves |
| `caps_word` `enter` / `exit` outputs | **Implemented, not yet flashed** |
| Battery / USB secondary indicators | **Present but disabled** — see `PATCH_PIXEL.md` |

## Naming convention

**Scope: this fork only.** Other modules pulled by `west.yml`
(`zmk-hold-morph-unicode`, `zmk-listeners`, `zmk-raw-hid`, …) are documented in
their own repos. Do not mirror their docs here — duplicated docs drift.

`PATCH_<TOPIC>.md` — one doc per feature, no version suffixes. Each doc is the
single current source of truth for its feature; supersede in place rather than
adding `_vNNN` files. Functional config files (`west.yml`, `main.yml`) are never
prefixed.

`PIXLBLINK` is deliberately spelled without the `E` so searches for it never
collide with `PIXEL` / `&pixel` results.

## The one rule to never break

The split RGB payload (`set_rgb_pixel` in `types.h`) **must stay ≤ 20 bytes**.
It is currently 16. Exceeding it silently kills *every* RGB pixel command to the
right half — `&pixel` included. Full explanation in `PATCH_ARCHITECTURE.md` §5.1.
