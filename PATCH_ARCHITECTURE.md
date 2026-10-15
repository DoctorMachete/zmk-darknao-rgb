# PATCH — ARCHITECTURE & CONSTRAINTS (shared)

Foundations every RGB feature in this fork depends on. Read §5 before changing
anything in the split transport or adding a composite layer.

---

## 1. Repo & build topology

- **Fork:** `zmk-darknao-rgb` (darknao/zmk, branch `rgb-layer-24.12`), DoctorMachete
  mirror. Zephyr 3.5.0, SDK 0.16.9.
- **Config repo:** `GO60-WEST-finegrained` → `config/` holds `go60.keymap`,
  `go60.conf`, `go60_lh.overlay`, `go60_rh.overlay`, `darknaofork_patches.dtsi`,
  `west.yml`.
- **Strip geometry:** LH chain-length 30, RH chain-length 30.
- `app/src/rgb_underglow.c` compiles into **both** halves (gated only on
  `CONFIG_ZMK_RGB_UNDERGLOW`). Each half runs its own render tick and composite.
  **This is the key architectural fact: the peripheral animates autonomously** —
  the central pushes a *spec* once, it never streams frames.

### 1.1 CI cache = SHA-pin model
Cache key is `hashFiles('config/west.yml')` only; the full workspace
(`zephyr/ tools/ modules/ zmk/ bootloader/`) is cached. The fork is **SHA-pinned**
in `west.yml`, so bumping that pin is what invalidates the cache — precisely and
only when you pick up new firmware. The older "push any commit → auto-invalidate"
(Option-B tip-watching) flow is **gone**.

**To pick up new firmware:**
```
git ls-remote https://github.com/DoctorMachete/zmk-darknao-rgb rgb-layer-24.12C
# paste the new tip SHA into west.yml, commit
```
That commit is both the trigger and the cache invalidation.

**Speed model:** keymap-only push → warm cache, fast. Editing any branch-tracked
module → full cold rebuild on both board jobs.

**Drift caveat:** the *other* DoctorMachete modules (`zmk-listeners`,
`zmk-raw-hid`, `zmk-keypeek-layer-notifier`, adaptive-sequence, dynamic-macros,
prospector, force-case, bootloader-usb-guard, …) remain **branch-tracked**. If one
moves upstream without `west.yml` changing, the cache serves a stale copy. If a
previously-good build breaks with no fork change, suspect module drift first.
Consider SHA-pinning them.

### 1.2 Deploy ritual
- Fork source change ⇒ rebuild the fork **and bump the SHA in `west.yml`**.
- Keymap-only change ⇒ rebuild, no SHA bump.
- **Always reflash BOTH halves.** A merged both-halves artefact is fine; each side
  takes its own slice.

---

## 2. The RGB composite stack

Priority, low → high:

```
underglow / per-layer RGB      pixels[]
  └─> &pixel static overrides  pixel_overrides[] / pixel_override_active[]
        └─> PIXLBLINK blink    pixlblink_slots[]
              └─> MAGIC        magic_pixels[] / magic_pixel_active[]   ← wins
```

Each layer has its own buffer and its own `any_*` fast-path flag. Clearing an upper
layer **reveals** the layer beneath rather than blanking the LED.

Stamping happens in `zmk_led_generate_status()`, in that order; the result is
applied to the strip by `zmk_led_write_pixels()` via `status_pixels[]` +
`status_overlay_active[]`.

## 3. Timers

| timer | period | drives |
|---|---|---|
| `underglow_tick` | 25 ms (50 ms in fade) | underglow / layer RGB effects; runs only while `state.on` |
| `underglow_status_update_timer` | 25 ms | status display, persistent indicators, **and PIXLBLINK animation** |

`zmk_rgb_underglow_status_update()` keeps itself alive while
`any_persist_indicator() || any_pixlblink`.

## 4. ⚠️ The three gates — every overlay layer needs all three

When adding a composite layer, wire its `any_*` flag into **all three** or it
silently half-works:

1. **Compositing** — `persist_overlay` + the blend-trigger condition in
   `zmk_led_write_pixels()`.
2. **Power** — `desired_state` in `zmk_rgb_set_ext_power()`:
   ```c
   int desired_state = state.on || state.status_active || any_pixel_override ||
                       any_magic_pixel || any_pixlblink || any_persist_indicator();
   ```
   *Omit and the layer is invisible whenever underglow is off.*
3. **Animation** — the `persist` condition in `zmk_rgb_underglow_status_update()`,
   if the layer animates. *Omit and it paints one frame then freezes.*

Both omissions have already happened once each (MAGIC → gate 2; blink → gate 3).

---

## 5. Split transport

Cross-half plumbing shared by all RGB features.

- **Command:** `ZMK_SPLIT_TRANSPORT_CENTRAL_CMD_TYPE_SET_RGB_PIXEL`.
  GATT characteristic UUID **`0x00000008`**, write-without-response.
- **Path:** behavior (central, GLOBAL locality) → `zmk_split_central_*` → enqueue →
  BLE write → peripheral **msgq** (depth 16, drained in order) →
  `zmk_rgb_underglow_*` on the peripheral strip. Local pixels are applied
  synchronously in-process; only the remote half is queued.
- **Two peripheral dispatchers**, both must handle every op:
  `app/src/split/bluetooth/service.c` (BLE — the live path) and
  `app/src/split/peripheral.c` (generic/wired).

### Ops (`enum zmk_split_rgb_pixel_op`)

| op | val | payload used |
|---|---|---|
| `SET` | 0 | position, color |
| `CLEAR_ONE` | 1 | position |
| `CLEAR_ALL` | 2 | — |
| `BATTERY` | 3 | `u.positions[]`, count |
| `BATTERY_CLEAR` | 4 | — |
| `USB` | 5 | position |
| `USB_CLEAR` | 6 | — |
| `PIXLBLINK` | 7 | position, color (=color1), `u.blink.color2`, `u.blink.freq_code`, `u.blink.blinks` (0 = until cleared) |
| `PIXLBLINK_CLEAR` | 8 | position |
| `SET_MANY` | 9 | color, count, `u.positions[]` — built only by the central's send loop (§5.4) |
| `CLEAR_MANY` | 10 | count, `u.positions[]` — built only by the central's send loop (§5.4) |

### 5.1 ⚠️ THE 20-BYTE CEILING — the most important constraint in this fork

`bt_gatt_write_without_response()` is capped at **ATT_MTU − 3 = 20 bytes**
(Zephyr default MTU 23; this fork sets **no** MTU override anywhere).

ZMK is built around that ceiling: `zmk_split_run_behavior_payload` is `__packed`
and lands on *exactly* 20 bytes — which is why `ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN` is
the oddly specific value **9**.

| payload | size | result |
|---|---|---|
| key presses (`run_behavior`) | 20 | fits exactly |
| layer RGB (`set_rgb_layers`) | 4 | fits |
| `set_rgb_pixel` **(current)** | **16** | fits |
| `set_rgb_pixel` (interim, flat fields) | 24 | **rejected — killed every pixel write** |

**Current payload — 16 bytes via a union:**
```c
struct {
    uint8_t  op;        // @0
    uint8_t  position;  // @1   local strip index on the peripheral
    uint8_t  count;     // @2   battery op only
    uint32_t color;     // @4   0xRRGGBB — also color1 for pixlblink
    union {             // @8   positions[] and the blink extras are never
        uint8_t positions[8];            //   used by the same op, so they
        struct {                          //   share storage.
            uint32_t color2;      // @8
            uint8_t  freq_code;   // @12
            uint8_t  blinks;      // @13  blink-count; was padding (the struct is 8 bytes
        } blink;                  //      either way)
    } u;
} set_rgb_pixel;        // sizeof == 16
```

**Checked at build time** (since the blink-count patch, `app/src/split/bluetooth/service.c`):
the peripheral's mirror `rgb_pixel_msg` has the wire struct's size and field offsets, and
is ≤ 20 bytes. A layout slip or an oversize payload now fails the right-half build instead
of silently killing pixel writes.

**Rule:** never let `set_rgb_pixel` exceed 20 bytes. More per-op state → overlap it
into the union, or raise `CONFIG_BT_L2CAP_TX_MTU` on **both** halves (changes split
ATT negotiation; needs its own testing).

**Diagnostic signature:** the write is rejected *before transmission*, so nothing
reaches the peripheral — **every** RGB pixel op on the right half dies at once,
while keys and per-layer RGB keep working because their payloads are small. If you
see that asymmetry, check payload size first.

### 5.2 ⚠️ `__packed` does not propagate into a named nested member

The outer `zmk_split_transport_central_command` is `__packed`, but the **named**
member `set_rgb_pixel` inside its union is laid out with **natural alignment**
(`color` at offset 4, not 3).

The peripheral mirrors the payload as its own `rgb_pixel_msg` struct in `service.c`
and fills it with a raw `memcpy`. **Both definitions must stay byte-identical and
both UNPACKED.** Adding `__packed` to only the peripheral side moves `color` 4 → 3
and corrupts every message.

Verified layout (both sides): `op@0 position@1 count@2 color@4 u@8`, `sizeof == 16`.

### 5.3 ⚠️ The left-half send queue

Every command to the right half (pixel ops, per-layer RGB state, relayed behaviors) goes
through one queue on the central, `zmk_split_central_split_run_msgq`, drained by its own
thread one BLE write at a time. Its depth is
`CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE` (**default 5**). When `k_msgq_put()`
can't get a slot within 100 ms, `split_bt_invoke_behavior_payload()` **discards the
oldest queued command** and retries. Nothing is logged above `WRN`.

Observed: the RNPAD idle-lock `exit` (11 right-half `&pixel` clears, sent at the same
moment as the layer-state update) left the 6th–8th commands undone. Fixed in the keymap
repo with `CONFIG_ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_QUEUE_SIZE=24` in `go60_lh.conf` (central
only; about 24 bytes per slot).

Even with no drops, the writes reach the right half only when it listens (§5.5), about 3
per listening window (the left-half host has `BT_BUF_ACL_TX_COUNT=3`), so a chain of ~10
right-half commands lands in visible batches. §5.4 reduces the number of writes, and §5.5
makes the windows frequent enough that the batches blur together.

The right half's receive queue (`rgb_pixel_msgq`, depth 16, `service.c`) also drops the
oldest entry when full, but it is drained immediately and has not been seen to overflow.

### 5.4 Send-time pixel grouping

**Status: on hardware. It cut the RNPAD idle-lock unlock from 12 writes to 4 (mostly 3
stages → mostly 2), but did not remove the stages by itself; §5.5 did.**

`split_central_split_run_callback()` (`app/src/split/bluetooth/central.c`) merges a single
`SET` (or `CLEAR_ONE`) with the commands queued **right behind it** that do the same thing
— same peripheral, same op, same colour — into one `SET_MANY` (`CLEAR_MANY`) write of up to
8 pixels in `u.positions[]`. Nothing in the keymap or the behaviors changes; every chain
that targets the right half (idle lock, layer listeners, caps lock, macro slots) benefits.

Why it works: behaviors and listeners run on the system work queue, which is cooperative
(priority −1); the send loop runs on its own preemptible thread (`ZMK_BLE_THREAD_PRIORITY`,
5). So a whole `enter` / `exit` chain is queued before the loop wakes up, and the loop sees
it all at once.

| RNPAD idle lock | before | after |
|---|---|---|
| `enter`: 10 orange + 1 blink | 11 writes | 3 (8 + 2 orange, 1 blink) |
| `exit`: 10 clears + 1 blink-off + layer update | 12 writes | 4 |

Rules (enforced by `split_run_pixel_joins()`):
- Only **adjacent** commands merge, so nothing is reordered. The first command that
  doesn't fit is held in `split_run_carry` and sent next.
- Different colours don't merge. Blinks, `CLEAR_ALL`, layer updates and behavior relays
  never merge and keep their order.
- A command with nothing to merge with is sent **byte-identical** to before.
- Payload unchanged: 16 bytes (`count` and `u.positions[8]`, as the battery op uses).
  Constraint #1 holds.
- `split_run_group_pixels()` is `__noinline`: the send thread's stack is small
  (`ZMK_SPLIT_BLE_CENTRAL_SPLIT_RUN_STACK_SIZE`, 512). Measured with `-fstack-usage`: the
  loop's frame goes 72 → 80 bytes; the helper's 80 bytes exist only while grouping, when no
  BLE call is on the stack.

Receivers: `split_svc_update_rgb_pixel_callback()` (BLE, `service.c`) and
`zmk_split_transport_peripheral_command_handler()` (wired/generic, `app/src/split/peripheral.c`) both
handle `SET_MANY` / `CLEAR_MANY` by looping `zmk_rgb_underglow_set_pixel()`. **Both halves
must run the new firmware** — an old right half logs "Unknown RGB pixel op" and ignores a
grouped message.

Test: `app/tests-host/split-pixel-grouping/run.sh` (host gcc only) copies the grouping code
verbatim out of `central.c` and checks it against a model of the right half: the RNPAD
cases above, colour splits, the 8-pixel cap, interleaved other commands, and 200,000
random command mixes with the loop waking at random points. It asserts the right half ends
in exactly the state that one-by-one sending gives, and that non-pixel commands keep their
order. A deliberately broken colour check makes it fail.

### 5.5 ⚠️ Peripheral latency: the real bottleneck for left → right updates

The split link runs at a 7.5 ms interval (`ZMK_SPLIT_BLE_PREF_INT=6`) with **peripheral
latency** `ZMK_SPLIT_BLE_PREF_LATENCY` (default **30**). In Zephyr's controller
(`ull_conn.c`, `ull_conn_done()`), after any exchange in which the right half has nothing
of its own to send, it skips the next `latency` connection events. Receiving data from the
central does **not** keep it awake. So with the default, the right half only listens about
every 31 × 7.5 ms ≈ 232 ms when idle, and anything the left half sends waits for that.
Messages that don't fit in one window wait another ~232 ms.

Observed on hardware (RNPAD idle-lock unlock, whose `exit` and the layer-state update go out
together): distinct stages about 0.2 s apart, with the per-layer colours and the `&pixel`
lights changing in different windows. Grouping (§5.4) and trimming the right-half pixels
reduced the stages but did not remove them.

**Fix, in the keymap repo's `go60_lh.conf`** (the central requests the parameters; the
right half does not ask for its own, `BT_GAP_AUTO_UPDATE_CONN_PARAMS` is off there):

```
CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY=5     # right half listens every ~45 ms while active
```

Result: unlock is instant or a quick fade; no separate stages. It also speeds up every other
left → right update (per-layer colours, caps-lock LED, macro LEDs, relayed behaviors).

**Cost (estimate, not measured):** about 18 more right-half wake-ups per second while active,
at a few µC each with `BT_CTLR_TX_PWR_PLUS_8` ⇒ roughly 0.05–0.15 mA extra on the **right
half only**, and only while active. After `ZMK_IDLE_TIMEOUT` (30 s) the central switches
to the separate idle parameters (`ZMK_SPLIT_BLE_PREF_IDLE_INT=18`, `_IDLE_LATENCY=10`),
which this setting does not touch. Small next to powered RGB LEDs; noticeable only with
RGB off. Raise it (8–10) to trade some responsiveness back for battery.

Considered and declined: making lock lights part of the per-layer map (a lock-aware
`&ug_lk` colour, with lock bits sent alongside the layer state) would have needed no
latency change, but means editing per-layer maps for every locked layer.

---

## 6. Addressing

Header: `app/include/dt-bindings/zmk/go60-pixels.h` — include with **angle
brackets**; quoted/relative includes are unreliable under Nix.

- **Global positions: `0..29` = LEFT (central), `30..59` = RIGHT (peripheral,
  relayed as `pos − 30`).** The split boundary is `zmk_rgb_underglow_pixel_count()`
  = the local `STRIP_NUM_PIXELS`.
- Names (`PIXEL_LH_*` / `PIXEL_RH_*`) were derived by inverting the `pixel-lookup`
  tables in `go60_lh.dts` / `go60_rh.dts`. **Indices are non-contiguous in key
  order** — always use the names, never `+1` arithmetic.
- `PX_*` colour macros are always in scope.
- Group-clear sentinels: `PIXEL_CLEAR_LEFT 250`, `PIXEL_CLEAR_RIGHT 251`,
  `PIXEL_CLEAR_ALL 252` — must stay in sync with `ZMK_PIXEL_CLEAR_*` in
  `rgb_underglow.h`.

---

## 7. Devicetree & keymap constraints

- **A behavior node must exist or its driver compiles out.** Every behavior is
  wrapped in `#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)`. No node with that
  `compatible` anywhere in the merged tree ⇒ the driver disappears and
  `&thebehavior` is an undefined label ⇒ **the whole keymap fails to build**.
- **Behavior/preset nodes go in a `behaviors { }` block**, never inside
  `layer_listeners { }` — the listeners module compiles every child of
  `layer_listeners` *as a listener* (`DT_INST_FOREACH_CHILD`) and the build dies in
  `LAYER_LISTENER_INST` macro expansion.
- Custom fork behaviors live in the top-level `behaviors { }` block inside the
  `#ifdef TESTGITHUB` guard, next to `magic_status_ble`.
- **`#define TESTGITHUB YES` must stay uncommented.** The entire custom DT area —
  the `go60-pixels.h` / `magic-indicator.h` includes, `darknaofork_patches.dtsi`,
  and the whole `layer_listeners` block — sits behind it. Commenting it out
  silently removes `&pixel`, the magic behaviors, and every listener at once.
- **Colour macros:** inside the listener block use `PX_*` names or raw `0xRRGGBB`.
  The keymap's `*_RGB` names are `#undef`'d before that block and will not resolve.
- **`POS_*` (key positions) are NOT strip indices.** Use `PIXEL_LH_*` / `PIXEL_RH_*`.
- A right-half pixel lighting in the wrong place is a `PIXEL_RH_*` value to fix in
  `go60-pixels.h`, **not** a transport bug.

### 7.1 Event listener order is link order

ZMK runs `ZMK_SUBSCRIPTION` listeners in **link order**: `.event_subscription` is a plain
`KEEP()` section (`app/include/linker/zmk-events.ld`), never sorted. For the `app` target
that is the order of `target_sources()` lines in `app/CMakeLists.txt`. A listener that
must see raw key events before hold-tap / combo capture and replay them has to be listed
above them; `layer_idle_lock.c` is, deliberately (`PATCH_LAYER_IDLE_LOCK.md` §4.4). To
check a build: `arm-zephyr-eabi-nm -n zmk.elf | grep zmk_event_sub_.*position_state_changed`.
`behavior_caps_word` also appears in that list (after hold-tap, before combo / keymap): it
only timestamps key activity for its idle timeout and always bubbles, so its place is not
load-bearing (`PATCH_CAPS_WORD_IDLE_TIMEOUT.md` §2.4).

### 7.2 Layer locks

Since `PATCH_LAYER_LOCKING.md` (upstream ZMK semantics): stock `&tog` and `&to` **lock**
the layer they turn on, `&mo` does not, and a locked layer ignores every non-locking
deactivation (`&mo` / `&sl` release, conditional layers, `zip_temp_layer`, modules calling
the one-argument `zmk_keymap_layer_deactivate()`). Only a locking behavior turns it off.
`&tog` on a layer that is on but unlocked (held by `&mo`, opened by the trackpad) locks it
rather than closing it. So a layer that "won't turn off" was most likely turned on, or
tapped, with a locking `&tog` / `&to`.

A lock-only change raises `zmk_layer_lock_changed`, never a second `layer_state_changed`
(upstream re-raises the latter; here it would double-fire layer listeners). Custom
instances without `locking;` keep the 24.12 behaviour. The MoErgo build has no locks.

---

## 8. Bug ledger — why the code looks like this

| # | symptom | root cause | fix |
|---|---|---|---|
| 1 | MAGIC indicators invisible | `any_magic_pixel` was in the compositing gates but **not** the ext-power gate, so with underglow off the strip was never powered | add it to `desired_state` (§4) |
| 2 | `&pixel` dead everywhere | `//#define TESTGITHUB` removed the whole custom DT area including `layer_listeners` | uncomment it |
| 3 | `&pixlblink` undefined label | keymap still used the removed 2-cell form for clears | use `&pixlblink_off` |
| 4 | build died in `LAYER_LISTENER_INST` | blink presets declared inside `layer_listeners { }` | move to `behaviors { }` |
| 5 | **right half dead for all pixel ops** | payload grew 16 → 24 bytes, past the ATT ceiling; writes rejected before transmission | union `positions[]` with the blink fields → back to 16 (§5.1) |

**Note on #5:** two wrong fixes preceded the right one — a `__packed` change that
*created* an offset mismatch (§5.2), then a flashing/version theory. The clue that
cracked it: per-layer RGB colours still changed on the right half, proving the
peripheral ran the new firmware and the link was healthy, so the fault had to be
specific to the *pixel* command — and the only thing special about it was its size.

**Diagnostic lesson:** prefer symptom-differential reasoning (what still works vs
what doesn't) over re-verifying code already proven correct.

**Logging:** ZMK logs need `CONFIG_ZMK_USB_LOGGING=y` plus a serial console on the
board. GitHub Actions only compiles — it cannot show runtime logs.
