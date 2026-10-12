# PATCH — caps word idle timeout (`idle-timeout-ms`)

**Status: implemented and tested off-hardware, not yet flashed.** 8 native_posix cases,
the full test suite, and both GO60 target builds (see §8). Adds `idle-timeout-ms` to
`zmk,behavior-caps-word`: caps word turns itself off after that long without key activity,
and fires `exit` like any other deactivation.

Shared background: `PATCH_ARCHITECTURE.md`. Builds on `PATCH_CAPS_WORD_OUTPUTS.md` (same
file, same `exit` chain). Prior art: QMK's `CAPS_WORD_IDLE_TIMEOUT`. Upstream ZMK has no
caps-word timeout (checked against upstream `main`, 2026-10-12).

---

## 1. What it does

```dts
caps_word {
    continue-list = < ... >;
    idle-timeout-ms = <5000>;
    exit = <&pixlblink_off PIXEL_LH_T2>;
};
```

| you do | firmware |
|---|---|
| turn caps word on | timer starts at `idle-timeout-ms` |
| keep typing, any gap shorter than the timeout | caps word stays on, however long the word |
| stop for `idle-timeout-ms` | caps word turns off, `exit` fires (the indicator stops) |
| turn it off yourself, or type a word-breaking key | as before; the timer is cancelled |

- **Per instance.** Each caps-word behavior has its own value and its own timer.
- **`0` or absent = no timeout**: the stock behaviour. A keymap that doesn't set it
  behaves exactly as before.
- The timeout measures a **pause**, not the length of the word: each bit of activity
  restarts the count.

### 1.1 What counts as activity

| counts | doesn't count |
|---|---|
| any key **press or release**, either half, whatever it's bound to (a layer key, `&none`, a hold-tap still undecided) | **holding** a key: only its press and its release count, so a key held longer than the timeout lets caps word time out mid-hold (test 7) |
| any key code **sent** while caps word is on, even with no key touched (a macro, dynamic-macro playback) | pointer motion (trackpads, mouse keys) |
| the activation itself | anything while caps word is off |

That is QMK's rule (any key press restarts the timer), plus releases and sent key codes,
which cost nothing and remove two surprises: a macro that types slowly, and a layer key
pressed and released between letters.

## 2. Firmware: `app/src/behaviors/behavior_caps_word.c`

### 2.1 Config and data

```c
struct behavior_caps_word_config {
    ...
    uint16_t wait_ms;
    int32_t idle_timeout_ms;          /* new; 0 = no timeout */
    uint8_t continuations_count;
    struct caps_word_continue_item continuations[];   /* flexible array: stays last */
};

struct behavior_caps_word_data {
    bool active;
    int64_t last_activity;            /* uptime of the last activity while active */
    const struct device *dev;         /* back-pointer for the timer handler */
    struct k_work_delayable idle_work;
};
```

The driver gained an init function (`behavior_caps_word_init`, was `NULL`) that sets the
back-pointer and initialises the delayable work.

### 2.2 Timer life cycle

All of it hangs off the existing chokepoint, `set_caps_word_state()`:

- **on** (`idle-timeout-ms > 0`): stamp `last_activity = now`, schedule the timer for the
  full timeout, then fire `enter`.
- **off**, by any route (second press, word break, the timeout itself): cancel the timer,
  then fire `exit`.

### 2.3 Activity only stamps; the timer re-checks

Activity does **not** move the timer. It only writes `last_activity`
(`caps_word_note_activity()`, every active instance). When the timer expires,
`caps_word_idle_timeout()` measures from the stamp:

```c
int64_t remaining = data->last_activity + config->idle_timeout_ms - k_uptime_get();
if (remaining > 0) {
    k_work_reschedule(&data->idle_work, K_MSEC(remaining));   /* wait out the rest */
    return;
}
deactivate_caps_word(dev);
```

Two reasons for this shape over "reschedule on every key":

1. **It closes a race.** The timer and key processing share the system work queue. If the
   timer expires while a key event is already queued ahead of it, a reschedule from that
   key event would not un-queue the expired handler (`k_work_reschedule()` doesn't
   un-submit), and caps word would turn off right after a key. Here the handler runs after
   the key, sees the fresh stamp, and waits.
2. One timer operation per timeout period while typing, instead of one per key.

Measured in the test harness, the timeout lands 0–2 ms after `last activity + timeout`
(tick rounding).

### 2.4 Listener

The listener (renamed `caps_word_listener`) now takes two events:

- `zmk_position_state_changed` (**new subscription**): stamp, bubble. Nothing else.
- `zmk_keycode_state_changed` (as before): on a press, stamp, then the existing
  enhance/break logic. A breaking key cancels the timer through `deactivate_caps_word()`.

**Link order** (`PATCH_ARCHITECTURE.md` §7.1): `behavior_caps_word.c` is listed after
`behavior_hold_tap.c` and before `combo.c` / `keymap.c`. So caps word sees positions a
hold-tap captured once they are replayed (≤ the tapping term later, harmless for a
timeout in seconds), and sees combo presses before the combo consumes them. Order matters
little here anyway: the listener only stamps a time and always bubbles. Checked on the LH
build:

```
zmk_event_sub_behavior_hold_tapzmk_position_state_changed
zmk_event_sub_behavior_caps_wordzmk_position_state_changed
zmk_event_sub_behavior_tap_dancezmk_position_state_changed
zmk_event_sub_keymapzmk_position_state_changed
```

### 2.5 Threading

Key events, the behavior queue and the timer all run on the system work queue, so the
state needs no lock (same reasoning as `layer_idle_lock.c`). On a timeout, `exit` runs
from the timer's work item instead of from inside a key event; for the pixel behaviors
(`not-press-release-outputs`) that makes no difference.

## 3. DT binding: `app/dts/bindings/behaviors/zmk,behavior-caps-word.yaml`

```yaml
idle-timeout-ms:
  type: int
  default: 0
```

A negative value fails the build (`BUILD_ASSERT` in `KP_INST`).

## 4. Constraints

### 4.1 Architecture constraints touched

- **#1 (20-byte split payload): untouched.** Nothing new crosses the split; the right
  half's keys reach the central as position events anyway.
- **#4 (node must exist):** unchanged. `&caps_word` survives because the keymap
  references it from a hold-tap.
- **#6 (`TESTGITHUB`):** the property is fork-only, so it goes inside the existing
  `#ifdef TESTGITHUB` in the `caps_word` node (§5).
- **#11 (listener order):** a new position subscriber; see §2.4.
- **Deploy ritual:** fork source change ⇒ rebuild **and bump the fork SHA in
  `west.yml`**, then reflash both halves.

### 4.2 New hazards

1. **A held key doesn't keep caps word on** (§1.1). Choose a timeout comfortably longer
   than any key you hold mid-word.
2. **Pointer motion doesn't count.** Reaching for the trackpad mid-word is a pause.
3. **`exit` can now fire outside a key event**, at the timeout instant. Indicators don't
   care. Key output on `exit` would be sent then, unprompted; the tests use `&kp F14` on
   `exit` only to make deactivations visible. (Key output on `enter` stays forbidden,
   `PATCH_CAPS_WORD_OUTPUTS.md` §5.2.)

## 5. Keymap wiring (GO60)

In the existing `caps_word` node, inside its existing `#ifdef TESTGITHUB`:

```dts
caps_word {
  continue-list = < ... >;
  #ifdef TESTGITHUB
    not-press-release-outputs;
    idle-timeout-ms = <5000>;
    enter = <&blink_capsword PIXEL_LH_T2    &blink_capsword PIXEL_RH_T2>;
    exit  = <&pixlblink_off  PIXEL_LH_T2    &pixlblink_off  PIXEL_RH_T2>;
  #endif
};
```

5000 ms is QMK's default; tune after use. The `exit` chain already stops the blink, so
the indicator follows the timeout with no other change. The MoErgo build doesn't see the
property (guard) and keeps caps word without a timeout.

## 6. Files

**Modified (2):** `app/src/behaviors/behavior_caps_word.c`,
`app/dts/bindings/behaviors/zmk,behavior-caps-word.yaml`.
**New:** this doc; `app/tests/caps-word-idle-timeout/` (8 cases + shared
`behavior_keymap.dtsi` and `events.patterns`).
**Docs touched:** `PATCH_00_INDEX.md`, `PATCH_CAPS_WORD_OUTPUTS.md` (§3.1 struct, §5.2),
`PATCH_ARCHITECTURE.md` (§7.1).

## 7. Tests: `app/tests/caps-word-idle-timeout/`

Timeout 500 ms; `exit = <&kp F14>` so each deactivation shows in the key log (0x69).

| case | checks |
|---|---|
| `1-idle-deactivates` | idle past the timeout ⇒ F14, next letter unshifted |
| `2-typing-keeps-it-on` | five letters 450 ms apart (2.2 s) stay shifted; a word break fires F14 once, and no second F14 follows |
| `3-key-without-keycode-is-activity` | a `&none` press/release keeps it on |
| `4-toggle-off-on-restarts` | off then on again: the timer restarts from the second activation |
| `5-no-timeout-by-default` | no property: 10 s idle, still shifted |
| `6-timeout-then-reactivate` | times out, turns on again, times out again |
| `7-held-key-still-times-out` | pins §1.1: a held key times out mid-hold |
| `8-macro-keycodes-are-activity` | a macro typing three letters 410 ms apart, no key touched: all shifted |

## 8. Verification

- The 8 cases above, snapshots reviewed line by line; timeout instants checked against
  the raw log (§2.3).
- **Mutation checks**, each caught by the case meant for it: drop position activity ⇒
  case 3 fails; drop key-code activity ⇒ case 8 fails; drop the stamp re-check ⇒ case 2
  fails.
- Upstream `app/tests/caps-word/` (5 cases): pass.
- Full suite (`run-test.sh all`): 273 pass, 1 pending (upstream's own `pending` case),
  no failures; 274 cases, the 266 from before plus these 8.
- Target builds `go60_lh` / `go60_rh` (fork's stock GO60 keymap, `&caps_word` bound on a
  thumb, wired as in §5): clean, no new warnings; `idle-timeout-ms` lands in the
  generated devicetree as `5000`.
- **Not done:** hardware. CI plus a flash is the final test.
