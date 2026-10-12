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
| **`PATCH_LAYER_LOCKING.md`** | Upstream ZMK layer locking, ported: stock `&tog` / `&to` lock, `&mo` doesn't, custom instances opt in with `locking;`. Lock changes raise `zmk_layer_lock_changed`. |
| **`PATCH_LAYER_IDLE_LOCK.md`** | `zmk,layer-idle-lock` — a layer held by `&mo` locks itself on after an idle hold, with `enter` / `exit` feedback chains; `triggers-on-any-lock` extends the feedback to `&tog` / `&to` locks. |
| **`PATCH_BUGFIX_HOLD_TAP_PRIOR_IDLE.md`** | Bug fix: hold-tap `require-prior-idle-ms` ≤ 0 ("disabled") could still force an instant tap after a macro-emitting hold-tap. |
| `PATCH_PIXEL_REFERENCE_go60-pixels.h` | Reference copy of the pixel-address header. **Nothing includes it** — the live header is `app/include/dt-bindings/zmk/go60-pixels.h`. |

## Feature status

| feature | state |
|---|---|
| `&pixel` | **Working**, both halves |
| MAGIC indicators | **Working**, LH (see LH-only caveat in its doc) |
| PIXLBLINK presets + off | **Working**, both halves |
| `caps_word` `enter` / `exit` outputs | **Implemented, not yet flashed** |
| hold-tap prior-idle ≤ 0 fix | **Implemented, not yet flashed** |
| layer locking (upstream semantics) | **Implemented, not yet flashed** |
| layer idle-lock (`zmk,layer-idle-lock`) | **Working on hardware**, LNPAD + RNPAD, both halves. Rebase onto layer locking + `triggers-on-any-lock`: **implemented, not yet flashed** |
| send-time pixel grouping (`PATCH_ARCHITECTURE.md` §5.4) | **On hardware**; fewer right-half writes. Smooth unlocks also need the latency setting in §5.5 |
| Battery / USB secondary indicators | **Present but disabled** — see `PATCH_PIXEL.md` |

## Naming convention

**Scope: this fork only.** Other modules pulled by `west.yml`
(`zmk-hold-morph-unicode`, `zmk-listeners`, `zmk-raw-hid`, …) are documented in
their own repos. Do not mirror their docs here — duplicated docs drift.

`PATCH_<TOPIC>.md` — one doc per feature, no version suffixes. Each doc is the
single current source of truth for its feature; supersede in place rather than
adding `_vNNN` files. Functional config files (`west.yml`, `main.yml`) are never
prefixed.

`PATCH_BUGFIX_<TOPIC>.md` — same rules, for a fix to existing behavior (typically code
inherited from upstream ZMK) rather than a new feature. The prefix keeps fixes
distinguishable from features at a glance.

`PIXLBLINK` is deliberately spelled without the `E` so searches for it never
collide with `PIXEL` / `&pixel` results.

## The one rule to never break

The split RGB payload (`set_rgb_pixel` in `types.h`) **must stay ≤ 20 bytes**.
It is currently 16. Exceeding it silently kills *every* RGB pixel command to the
right half — `&pixel` included. Full explanation in `PATCH_ARCHITECTURE.md` §5.1.
