# GO60-WEST-finegrained — Combined Project Handoff

> **ARTIFACT VERSIONS (this set):**
> - `PIXEL_HANDOFF_v001.md` (this file)
> - `PIXEL_MAGIC_INDICATOR_patch_v001.zip`
> - `PIXEL_MAGIC_INDICATOR_DESIGN_v001.md`
> - `PIXEL_KEYMAP_SNIPPET_v001.md`
>
> Versioning convention: RGB docs/patches carry a `PIXEL_` prefix and a
> zero-padded `vNNN` suffix; each family is numbered independently; bump on every
> regeneration and update this block. Functional config files (`main.yml`,
> `west.yml`) are NOT prefixed/versioned — they must keep their real names to
> function in CI.


_Last updated: end of the magic-indicator session. Two prior workstreams are
folded in: (A) the hold-morph + Windows-Unicode module, and (B) this session's
CI-caching rework + the new relocatable BLE/USB indicators. Sections 1–9 below
are split by workstream; read the part relevant to what you're touching._

> **CARRY-FORWARD RULE for any new chat:** re-upload the latest module zip(s)
> AND the current `go60.keymap` / `go60.conf` / `west.yml` so the next session
> inspects real files, not just this summary.

---

# PART -1 — Document lineage (READ FIRST)

Three source handoffs now exist; this combined doc reconciles them:
- **"GO60 Per-Pixel RGB" handoff** (the `&pixel` foundation) — describes the
  per-pixel feature ALL of this session's indicator work builds on. Still the
  authoritative reference for `&pixel` internals (addressing, compositing,
  split transport). Folded in as **PART D** below.
  - **SUPERSEDED PORTIONS:** its sections 7 ("Option B cache keying") and 9
    ("push any commit -> Option B auto-invalidates") describe the OLD caching
    model. That was REPLACED this session by the SHA-pin model — see **PART B1**.
    Where the pixel handoff and PART B1 disagree on CI, **PART B1 wins.**
- **"hold-morph + Unicode" handoff** — folded in as **PART A** (unchanged).
- **This session** — **PART B** (CI rework + magic-indicator feature).

---

# PART 0 — Repo / build topology (applies to everything)

- Keymap repo: **GO60-WEST-finegrained** (the manifest repo; `config/west.yml`
  is the manifest, `self.path: config`).
- Built against the **darknao/zmk** fork via doctormachete mirror, branch
  `rgb-layer-24.12C` (Zephyr v3.5.0+zmk-fixes, SDK 0.16.9).
- Many doctormachete modules are pulled in `west.yml` (listeners, raw-hid,
  keypeek, modifier-notifier, adaptive-sequence, dynamic-macros, prospector,
  hold-morph-unicode, unicode-font-mode, force-case, bootloader-usb-guard).
- CI builds **two board jobs per trigger** (`go60_lh` + `go60_rh`) plus a merge
  job — inherent to the split, not a bug.

---

# PART A — hold-morph + Windows Unicode module
_(unchanged this session; preserved verbatim from the prior handoff)_

## A1. What this module is
A ZMK module providing:
1. **`zmk,behavior-hold-morph`** — generalized multi-level mod-morph
   (`#binding-cells = <0>`): default action + N modifier-guarded children,
   strict exact-coverage matching, an `_AND_` operator for AND-groups, an
   AHK-style "unmatched mods -> nothing" fallback, and an optional **inline
   hold-tap** per node (`hold` + `tap`).
2. **Windows Unicode** `&unicode_1p` / `&unicode_2p` — `LALT + numpad-plus +
   hex` Alt-code input, with automatic UTF-16 **surrogate pairs** for code
   points > 0xFFFF.
3. **`zmk,behavior-mask-mods`** — internal helper so modifier masking is queued
   in playback order.

Published repo: `zmk-hold-morph-unicode`, byte-identical to the latest build.

## A2. File tree
```
zmk-hold-morph-unicode/
├── CMakeLists.txt
├── Kconfig
├── LICENSE                      (MIT; credits urob's zmk-unicode)
├── README.md
├── zephyr/module.yml            (dts_root: .)
├── dts/
│   ├── behaviors/unicode_win.dtsi          (&unicode_1p, &unicode_2p, &mask_mods)
│   └── bindings/behaviors/
│       ├── zmk,behavior-hold-morph.yaml
│       ├── zmk,behavior-unicode-win-1p.yaml
│       ├── zmk,behavior-unicode-win-2p.yaml
│       └── zmk,behavior-mask-mods.yaml
├── include/
│   ├── dt-bindings/zmk-holdmorph/hold_morph.h   (_AND_ op, ZMK_HOLD_MORPH_MAX_MORPHS=64)
│   ├── dt-bindings/zmk-holdmorph/unicode_win.h  (UCW_NONE sentinel)
│   └── zmk-holdmorph/unicode_win.h              (config struct, hex->key map, UCW_ALL_MODS)
└── src/behaviors/
    ├── behavior_hold_morph.c
    ├── behavior_unicode_win.c
    └── behavior_mask_mods.c
```

## A3. hold-morph design
- `#binding-cells = <0>`. Default action on the node + N named child morphs,
  each with `mods` + an action.
- **`mods` is `type: array`**; each element is one OR-group. **`_AND_`**
  (`#define _AND_ >, <`) splits cells so `mods = <(A) _AND_ (B)>` becomes
  `<(A)>, <(B)>` (all groups must hold).
- **Matching = exact coverage**: within a group mods OR; across groups all must
  hold; AND no held modifier may fall outside what the child claims.
- **Resolution (AHK-style default):** no mods -> default; exactly one child
  matches -> that child; mods held but zero/ambiguous match -> **nothing**
  (key swallowed). Opt-in `fallback-to-default;` restores legacy fall-back.
- Selecting mods masked from host (optional per-child `keep-mods`).
- **`ZMK_HOLD_MORPH_MAX_MORPHS = 64`** — compile-time BUILD_ASSERT; edit in
  `hold_morph.h`, NOT via keymap #define.

### Inline hold-tap (default node OR any child)
- Use `hold` + `tap` (renamed from `binding-hold`/`binding-tap`; old names
  REMOVED — breaking change). Each takes ONE behavior, up to TWO params.
- Both `phandle-array` with **`specifier-space: binding`** (required).
- A node uses EITHER `bindings` OR (`hold`+`tap`) — never both, never one.
- Per-node: `tapping-term-ms` (250), `flavor`, `require-prior-idle-ms` (0),
  `quick-tap-ms` (0).
- **`flavor` default is conf-configurable**: omitted -> Kconfig choice
  `CONFIG_ZMK_HOLD_MORPH_DEFAULT_FLAVOR_{TAP,HOLD}_PREFERRED` (built-in default
  hold-preferred). Per-node `flavor` overrides. (go60.conf currently sets
  `CONFIG_ZMK_HOLD_MORPH_DEFAULT_FLAVOR_TAP_PREFERRED=y`.)
- OMITS capture/replay (hold-trigger-key-positions, balanced flavor, retro-tap)
  — for those use a normal predefined hold-tap via `bindings`.

## A4. Unicode (Windows) design
- `&unicode_1p 0xE4` (1 cell), `&unicode_2p 0xE4 0xC4` (2 cells; param2 =
  shifted variant; UCW_NONE=0).
- `minimum-length` zero-pads hex per-instance.
- **Surrogate pairs**: <=0xFFFF one block; >0xFFFF UTF-16 pair
  (`U'=cp-0x10000; high=0xD800+(U'>>10); low=0xDC00+(U'&0x3FF)`). uint32 params.
- **Masking queued** via `mask_mods` (ALL & ~LALT before keystrokes, clear
  after).

## A5. Critical constraints (CARRY FORWARD)
1. **`CONFIG_ZMK_UNICODE_WIN_TAP_MS` and `_WAIT_MS` MUST stay 0** for unicode as
   a hold-tap TAP branch. Non-zero reschedules the queue mid-resolution and
   drops the tap. Currently unset -> 0 default applies.
2. **`_AND_` must be exactly `>, <`** — missing `<` => dtc "malformed value".
3. **Non-plural phandle-array props need `specifier-space: binding`** (`hold`,
   `tap`) — else dtc "name does not end in 's'".
4. **`mask_mods` node must exist, NOT `/omit-if-no-ref/`** — referenced by name
   through the queue. Supplied by `#include <behaviors/unicode_win.dtsi>`.
5. **`ZMK_HOLD_MORPH_MAX_MORPHS`** raised in `hold_morph.h` (64); keymap #define
   won't affect the BUILD_ASSERT.
6. Hold-tap-inside-tap-dance works (real finger-timed release) BUT the two
   tapping terms are ADDITIVE (hold needs tap-dance term + hold-morph term).
7. hold-morph single-press guard: one instance can't be in two overlapping
   activations.
8. `_AND_` is a short generic token — watch for keymap collisions. Provided by
   `#include <dt-bindings/zmk-holdmorph/hold_morph.h>` before DT parsing.

## A6. Keymap integration (confirmed working)
Inside `#ifdef TESTGITHUB` (defined) in `config/go60.keymap`:
```c
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk-holdmorph/hold_morph.h>   // _AND_
#include <behaviors/unicode_win.dtsi>               // unicode_1p/2p + mask_mods
```
Arrow hold-morphs keyed on MOD_LCTL/MOD_RCTL/etc.; `dm*_td` tap-dances ->
`dm*_s`/`dm*_d` hold-morphs -> inline hold-taps. Confirmed working.

## A7. Status — **confirmed working on hardware** (latest: hold/tap rename build).
In-session validation was WITHOUT a full firmware build (standalone C unit
tests, preprocessor macro tests, DT-macro existence checks vs Zephyr 3.5). The
user's CI + on-device test is the real validation.

## A8. hold-morph/unicode bugs fixed (all resolved)
Surrogate pairs added; async masking -> queued `mask_mods`; dropped nested-
hold-tap tap -> TAP/WAIT default 0; `_AND_` `>, ` -> fixed; missing
`specifier-space` -> added; AHK-style none default + `fallback-to-default`
opt-in; inline hold-tap defaults (hold-pref/250/prior-idle 0); `flavor` made
conf-configurable; MAX_MORPHS raised to 64; binding-hold/tap renamed to
hold/tap.

## A9. hold-morph/unicode open items
- None blocking. Optional: add explicit `CONFIG_ZMK_UNICODE_WIN_TAP_MS=0` to
  go60.conf as a regression guard. User A/B-testing `flavor` via the conf choice
  with `flavor` omitted; may later pin explicit flavors per node.

---

# PART B — THIS SESSION's work (CI caching + BLE/USB indicators)

## B1. CI caching: moved to the SHA-pin ("pre-current") model

**Problem solved earlier (Option-B):** a stale-cache bug had been worked around
by manually deleting touched files before re-upload. Option-B replaced that with
an all-or-nothing cache key that hashed `west.yml` + a derived `module-tips.lock`
(live `git ls-remote` of every doctormachete module tip). It was correct but
invalidated the ENTIRE workspace (incl. Zephyr) on any module-tip move, so every
fork push paid a full cold Zephyr recompile on BOTH board jobs — ~2x the minutes
vs the old manual-delete method (which only ever invalidated touched files and
kept Zephyr warm).

**Current model (what's deployed now):** reverted to a pre-Option-B style cache,
key = `hashFiles('config/west.yml')` only, full workspace cached
(`zephyr/ tools/ modules/ zmk/ bootloader/`). The fork is **SHA-pinned** in
`west.yml`, so bumping the pin is what changes the key and invalidates the cache
— precisely and only when you pick up new firmware. The tip-watching step was
removed entirely. (Patched `main.yml` + `west.yml` were delivered in the prior
session; they are also in `/mnt/user-data/outputs` as `main.yml` / `west.yml`.)

**Deploy flow change (IMPORTANT):** the old "push any commit, Option-B auto-
invalidates" is GONE. To pick up new firmware now:
`git ls-remote https://github.com/DoctorMachete/zmk-darknao-rgb rgb-layer-24.12C`
-> paste the new tip SHA into `west.yml` -> commit. That commit is BOTH the
trigger and the cache invalidation.

**Speed model:**
- keymap-only push -> exact-key HIT -> fully warm -> fast (only keymap recompiles).
- editing ANY watched module (fork OR listeners/etc.) while it's branch-tracked
  -> full cold rebuild on both jobs (~2x), same cost regardless of which module.
  This cost attaches to "you are mid-editing a watched module," not to the fork
  specifically.
- Once a module is frozen and you live in the keymap, you're back on the fast
  path automatically.

**SHA-pin caveat (intentional, documented in main.yml):** the OTHER
doctormachete modules remain BRANCH-tracked, so if one of their tips moves
upstream WITHOUT `west.yml` changing, the cache serves a STALE copy of that
module. Acceptable while they're stable deps you don't push to. If you start
iterating on one, bump anything in `west.yml` to force a refresh, or re-add
tip-watching.

**Current pinned fork SHA in west.yml:** `2efc27769d7b0f1d876b2b91835a57442dba285b`
(user has bumped it past the session's earlier `c286fbe3...`; the SHA-pin flow
is confirmed working in practice).

**Optional future optimization (NOT done):** layer ccache underneath the
workspace cache, or split Zephyr into its own stable cache entry keyed on the
upstream Zephyr revision. Either restores ~old speed on module edits WITHOUT the
manual ritual. ccache is the more robust general fix. Deferred — current model
is fine while editing is infrequent.

## B2. NEW FEATURE: relocatable BLE/USB indicators (`&magic_indicator_*`)

**Goal:** show the native Magic-layer BLE/USB connection indicators (4x BLE +
1x USB) at ARBITRARY keymap positions on EITHER half, driven by the layer
listener (enter -> show, exit -> hide), with colors reusing the native state ->
color ladder and updating LIVE while held. The existing `&pixel` behavior is
UNCHANGED (still arbitrary static colors).

**Status: BUILT, NOT YET COMPILED/FLASHED.** Compile-verified by inspection only
(no Zephyr build in sandbox). Delivered as `MAGIC_INDICATOR_patch.zip` +
`MAGIC_INDICATOR_DESIGN.md` + `KEYMAP_SNIPPET.md`. **First CI build is the real
validation.**

### Why it's small (key realization)
The earlier `&pixel` work already built the cross-half color channel:
`behavior_pixel_color.c` is GLOBAL-locality, runs on the central, splits the
global address space (0..local-1 = left/central, >=local = right/peripheral via
`position - local_count`), and pushes resolved colors to the peripheral with
`zmk_split_central_set_pixel(pos, color)`. The peripheral HOLDS that color in
`pixel_overrides[]` (split op `OP_SET` in `split/bluetooth/service.c`) and
repaints it every frame. So no new split protocol was needed.

### Key architectural decision: push RESOLVED COLOR, not a "BLE op"
The peripheral CANNOT compute BLE/USB color — endpoint/profile state is
central-only (`zmk_endpoints_selected()` doesn't exist on the peripheral; the
fork's own `OP_USB` path is a no-op there). So the central computes the native
color and pushes it as a plain resolved-RGB `OP_SET` (the `&pixel` path). No new
opcode, zero peripheral changes.

### Live updating
A small central-side registry (max 8) tracks active indicators `{position,
which}`. The layer listener fires only at enter/exit (it does NOT repaint while
held — confirmed in `layer_listeners.c`), so a dedicated `ZMK_LISTENER` in
`magic_indicator.c` subscribes to `ble_active_profile_changed`,
`endpoint_changed`, `usb_conn_state_changed` and repaints all registered
indicators on any change. Local dots would also be repainted by the native
render loop, but the registry handles both halves uniformly.

### Color ladder (mirrors native exactly; packed 0xRRGGBB)
- BLE profile i: status==2 AND active-endpoint-BLE AND active==i -> **white**;
  status==2 -> **dull_green 0x00ff68**; status==1 -> **red 0xff0000**;
  status==0 -> **lilac 0x6b1fce**.
- USB: HID+active -> white; HID -> dull_green; powered -> red; none -> lilac.

### Parameters / API
- `&magic_indicator_on  POSITION WHICH` — param1 = global pixel position (same
  address space as `&pixel`, so `PIXEL_LH_*` / `PIXEL_RH_*` work), param2 =
  WHICH ∈ {`MAGIC_USB`, `MAGIC_BLE0`..`MAGIC_BLE4`} (from
  `dt-bindings/zmk/magic-indicator.h`; BLE values are profile index 0..4,
  MAGIC_USB = 64).
- `&magic_indicator_off POSITION WHICH` — deregister + clear that pixel.
- `&magic_indicator_clear` — clear ALL active indicators (both halves).

### Files (in MAGIC_INDICATOR_patch.zip; unzip over zmk-darknao-rgb)
NEW:
- `app/include/dt-bindings/zmk/magic-indicator.h` (WHICH constants)
- `app/include/zmk/magic_indicator.h` (registry API)
- `app/src/magic_indicator.c` (registry, color resolution, routing, event listener)
- `app/src/behaviors/behavior_magic_indicator_on.c`
- `app/src/behaviors/behavior_magic_indicator_off.c`
- `app/src/behaviors/behavior_magic_indicator_clear.c`
- `app/dts/bindings/behaviors/zmk,behavior-magic-indicator-{on,off,clear}.yaml`
MODIFIED:
- `app/CMakeLists.txt` (4 `target_sources_ifdef(CONFIG_ZMK_MAGIC_INDICATOR ...)` lines)
- `app/Kconfig` (`config ZMK_MAGIC_INDICATOR`, default n, inside RGB_UNDERGLOW block)

### Enable + keymap wiring
- Add `CONFIG_ZMK_MAGIC_INDICATOR=y` to `go60.conf` (behind the flag, default
  off; cannot affect a build until enabled).
- Keymap pieces in `KEYMAP_SNIPPET.md`:
  1. `#include <dt-bindings/zmk/magic-indicator.h>` (near the go60-pixels.h
     include, ~line 256 — go60-pixels.h is already included).
  2. Three behavior nodes (`magic_indicator_on` #binding-cells=2,
     `_off` =2, `_clear` =0) inside the existing `behaviors { }` block.
  3. A `magic_status` child inside the existing `layer_listeners { }` block
     (~line 1337), `layers = <LAYER_Magic>` (LAYER_Magic = 27), enter = five
     `&magic_indicator_on` calls, exit = `&magic_indicator_clear`. Mirrors the
     existing `&pixel` enter/exit listener idiom.

### Design decisions made for the user (overridable)
- exit uses ONE `&magic_indicator_clear` (wipe all) rather than five symmetric
  `_off` calls. Snippet notes how to switch to per-dot teardown.
- Example positions in the snippet are placeholders (PIXEL_RH_C1R2..C5R2); swap
  for wherever the dots should live. Either half is allowed.

### Verification done (inspection only — NOT a build)
Confirmed against the real fork source: all event names/structs
(`zmk_endpoint_changed`, `zmk_ble_active_profile_changed`,
`zmk_usb_conn_state_changed` all `ZMK_EVENT_DECLARE`d), headers exist,
`zmk_endpoints_selected()` signature, `enum zmk_transport`
(`ZMK_TRANSPORT_USB/BLE`), USB conn enum (`ZMK_USB_CONN_NONE/POWERED/HID`),
`zmk_endpoint_instance.transport` field, `ZMK_EV_EVENT_BUBBLE`,
`zmk_behavior_get_empty_param_metadata`, `zmk_split_central_set_pixel/clear_pixel`
exports, `BEHAVIOR_DT_INST_DEFINE` macro, and brace/paren balance on all C files.
Used two separate single-compatible behavior files (idiomatic) after rejecting a
fragile combined-compatible file.

### Most likely first-build failure points (in order)
1. The `BEHAVIOR_DT_INST_DEFINE` invocations in the three behavior files.
2. The event-subscription block at the bottom of `magic_indicator.c`.
If it fails, the error will almost certainly name one of these.

### Behavioral caveat
The live-update listener repaints ALL active indicators on every BLE/endpoint/
USB change; each REMOTE-half repaint is a split BLE write. Trivial at 5
indicators; watch added split traffic only if scaled to many remote dots during
heavy connection churn.

### Reconciliation with the &pixel handoff (PART D) — three facts
1. **Indicators inherit the Runtime #3 composite fix for free.** magic_indicator
   paints via the SAME functions as `&pixel` (`zmk_rgb_underglow_set_pixel`
   locally, `zmk_split_central_set_pixel` remote), which set
   `pixel_override_active[]`. So indicator dots composite OVER the layer RGB via
   the `status_overlay_active[]` mask exactly like `&pixel` — they do NOT black
   out the strip. No separate compositing work was needed.
2. **Right-half indicators WORK in this design — and are better than the fork's
   disabled native `&usb_ind`.** The pixel handoff (§6.3) notes the native
   `persist_usb` path is a NO-OP on the peripheral because USB/BLE state is
   central-only. magic_indicator sidesteps that: the CENTRAL resolves the color
   and pushes the finished RGB via the `&pixel` OP_SET channel, so the peripheral
   never needs to know state. DO NOT "fix" this back onto the native
   `persist_usb`/`persist_ble` peripheral path — that reintroduces the no-op.
3. **Sequencing dependency:** magic_indicator shares the composite path with
   `&pixel`. If `&pixel`'s Runtime #3 composite fix is NOT yet confirmed on the
   current hardware (the pixel handoff marked it built-but-unflashed), confirm
   that FIRST — an indicator compositing bug would actually be a `&pixel`
   Runtime #3 bug, not an indicator bug.

## B3. THIS SESSION's open / pending items
- **PRIMARY:** flash + CI-build the magic-indicator feature. It is the only
  untested deliverable. User cannot update until tomorrow — files are bundled at
  the bottom of this handoff so they stay together.
- To deploy: unzip patch over `zmk-darknao-rgb`, add
  `CONFIG_ZMK_MAGIC_INDICATOR=y`, paste the three keymap pieces, commit, BUMP
  THE SHA in `west.yml`.
- Optional later: ccache / Zephyr-split cache optimization (B1) if module-edit
  build cost becomes annoying again.

---

# PART C — quick "where do I start" for the next session
1. If touching **hold-morph/unicode**: read PART A, re-upload that module zip +
   keymap/conf.
2. If touching **CI/build minutes**: read B1 (NOT the pixel handoff's §7/§9 —
   superseded). Remember the SHA-pin deploy ritual and the stale-other-modules
   caveat.
3. If touching **BLE/USB indicators**: read B2. It's built but UNCOMPILED — the
   patch zip is bundled below. First build is the test. Confirm `&pixel` Runtime
   #3 is flashed first (shared composite path).
4. If touching **`&pixel` itself / per-pixel internals**: read PART D.
5. Always re-upload real files (module zip(s), go60.keymap, go60.conf, west.yml)
   — don't work from this summary alone.

---

# PART D — `&pixel` per-pixel RGB foundation
_(condensed from the "GO60 Per-Pixel RGB" handoff; authoritative for `&pixel`
internals. Its CI sections are SUPERSEDED by PART B1 — see PART -1.)_

## D1. Goal & status
Set any individual LED on EITHER half to an arbitrary color, persistently,
COMPOSITING over the underglow/per-layer RGB (not replacing), regardless of RGB
on/off; independent of the per-layer RGB system.
**Status: WORKING, pending one more flash/test** — the Runtime #3 composite fix
was built but (at that handoff) not yet flashed. Confirm on BOTH halves.

## D2. Addressing (global 0–59)
- Each half drives its own 30-LED strip (STRIP_NUM_PIXELS=30 per controller).
- Global index: **0–29 = LEFT (central), 30–59 = RIGHT (peripheral)**, relayed
  as (index − 30). Behavior runs on the central (GLOBAL locality): writes local
  pixels directly, relays right-half writes over the split.
- Physical mapping is NON-contiguous (column-snaked, differs per half), derived
  by inverting the pixel-lookup table in go60_lh.dts/go60_rh.dts and
  cross-checked (top-left key = physical pixel 26). Names in
  `app/include/dt-bindings/zmk/go60-pixels.h`: `PIXEL_LH_*` (0–29),
  `PIXEL_RH_*` (30–59), `PX_*` colors, sentinels `PIXEL_CLEAR_LEFT/RIGHT/ALL`
  = 250/251/252 (mirror `ZMK_PIXEL_CLEAR_*` in rgb_underglow.h — keep in sync).

## D3. Compositing (the key architecture — Runtime #3)
Two buffers in rgb_underglow.c: `pixels[]` (live underglow/layer RGB) and
`status_pixels[]` (status/overlay; Magic display AND persistent overrides paint
here). `status_overlay_active[]` masks which positions the persistent layer
painted this frame.
- **Magic active** (`state.status_active`): whole-strip takeover (full replace /
  fade-blend) — a SEPARATE path, deliberately.
- **Persistent overlay** (`!status_active && (any_pixel_override ||
  any_persist_indicator())`): start from `pixels[]`, stamp ONLY
  `status_overlay_active[]` positions from `status_pixels[]`. Preserves layer RGB
  elsewhere. Applies on BOTH halves (same code).
- **Do NOT reintroduce a forced blend=256 for overrides** — that was the
  Runtime #3 bug (a single override blacked out the whole strip).

## D4. Cross-split transport
Added `SET_RGB_PIXEL` split command `{op, position, count, color, positions[8]}`,
ops SET / CLEAR_ONE / CLEAR_ALL / BATTERY / BATTERY_CLEAR / USB / USB_CLEAR.
Path: behavior -> `zmk_split_central_set_pixel/...` -> enqueue -> BLE write to
GATT UUID 0x00000008 -> peripheral **msgq** (Runtime #2 fix, depth 16, drains in
order) -> `zmk_rgb_underglow_set_pixel()` on the peripheral's strip. (Central
applies synchronously in-process, so only the right half ever raced.)

## D5. `&pixel` gotchas (carry forward)
1. **Color macro scope:** keymap `*_RGB` macros are #define'd only inside the
   per-key-RGB section and #undef'd after — UNDEFINED in the listener block. In
   `&pixel`/indicator bindings use `PX_*` or a raw `0xRRGGBB`, never `*_RGB`.
2. **Include:** `#include <dt-bindings/zmk/go60-pixels.h>` (angle brackets;
   quoted/relative is unreliable under Nix).
3. **POS_* ≠ strip index** — always use `PIXEL_LH_*`/`PIXEL_RH_*`.
4. **`(-1)` CLEARS an override** (returns to layer/underglow); `PX_OFF`/0x000000
   sets the pixel black but KEEPS the override. Different things. (magic_indicator
   clears with `-1`, correctly.)
5. **Clear sentinels** 250/251/252 must stay in sync with `ZMK_PIXEL_CLEAR_*`.
6. **Right half is peripheral-driven**; central-only concepts (USB endpoints) are
   guarded out on the peripheral. (This is why magic_indicator pushes resolved
   color from the central — see B2 reconciliation fact #2.)
7. **Both halves must be reflashed** after firmware changes touching either side.
8. A wrong-position right-half pixel = a `PIXEL_RH_*` value to fix in the header,
   NOT a transport bug.

## D6. `&pixel` bugs fixed (chronological)
CI#1 missing `path: zmk` (the real cause of the old delete-&-reupload ritual);
CI#2 stale module cache (was Option-B; now SHA-pin per B1); Link#1
`zmk_endpoints_selected` undefined on peripheral -> guarded; Runtime#1 any pixel
lit the Magic display -> indicators only when `status_active`; Runtime#2
peripheral dropped clears -> msgq; Runtime#3 override blacked out layer RGB ->
`status_overlay_active[]` composite (most recent; confirm on device).

## D7. `&pixel` pending (from that handoff)
- Confirm Runtime #3 on device, both halves (see D1).
- The disabled battery/USB indicator behaviors (`&bat_ind`/`&usb_ind`) are now
  effectively REPLACED for the USB/BLE case by this session's magic_indicator
  (B2), which also handles the right half. The old battery block (§6.2 there) is
  still unbuilt if you ever want relocatable BATTERY (which the user declined
  this session).
