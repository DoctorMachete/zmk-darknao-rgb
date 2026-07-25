# &pixlblink presets + &pixlblink_off — minimal both-sides test (v002)

Bench procedure for the v002 preset model. Assumes `&pixel` already works.

## 0. Spec under test
- Named preset blinks color1 <-> color2 (color2 may be black), sharp square wave.
- Frequency per preset: `freq_hz = frequency / 10` (5 = 0.5 Hz, 10 = 1 Hz).
- `&<preset> <global_pos>` starts; `&pixlblink_off <global_pos>` stops.

## 1. Build / flash
Fork source change: rebuild the fork and bump the SHA pin in `west.yml`. Flash BOTH halves.

## 2. Define two presets (in the keymap behaviors block, under #ifdef TESTGITHUB)
```
blink_a: blink_a { compatible = "zmk,behavior-pixlblink"; #binding-cells = <1>;
                   frequency = <5>;  color1 = <PX_ORANGE>; color2 = <PX_RED>; };
blink_b: blink_b { compatible = "zmk,behavior-pixlblink"; #binding-cells = <1>;
                   frequency = <10>; color1 = <PX_GREEN>; };   /* green <-> off @ 1 Hz */
```
(`&pixlblink_off` is shipped by the fork — don't redeclare it.)

## 3. Listener test (both halves)
```
blink_test {
    layers = <LAYER__SOMELAYER>;
    not-press-release-outputs;
    enter = <&blink_a PIXEL_LH_C3R2  &blink_b PIXEL_RH_C3R2>;
    exit  = <&pixlblink_off PIXEL_LH_C3R2  &pixlblink_off PIXEL_RH_C3R2>;
};
```

## What to look for
- LEFT pixel: orange<->red at 0.5 Hz (~1 s each). RIGHT pixel: green<->off at 1 Hz
  (~0.5 s each) — the two blink at DIFFERENT rates, confirming per-preset frequency.
- Leaving the layer stops both (reveals underglow / &pixel beneath).
- Two colors: confirm the OFF half is the second color, not forced black, when
  color2 is set (blink_a). With color2 omitted (blink_b) the off half is dark.

## Critical check — blink while RGB underglow is OFF
Turn underglow OFF, trigger a preset. It must still blink (ext-power gate includes
any_pixlblink; refresh timer stays alive). If nothing lights: the ext-power gate or
the timer lifecycle regressed — fix in rgb_underglow.c, not the keymap.

## Right side stays dark (only if LEFT works)
- Confirm the peripheral handles OP_PIXLBLINK / OP_PIXLBLINK_CLEAR with the new
  color2 + freq_code fields, and that the local rgb_pixel_msg struct is __packed
  (a layout mismatch would corrupt color/freq on the wire).
- Confirm both halves are on the SAME firmware build.

## Off behavior
`&pixlblink_off <pos>` clears just that position. Other blinking pixels keep going.
