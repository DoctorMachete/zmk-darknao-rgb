/*
 * KEYMAP INTEGRATION — &pixlblink presets + &pixlblink_off  (v002)
 * ===============================================================
 * v002 model: named presets (each with two colors + a frequency), invoked with
 * ONE param (the position). Plus the fixed &pixlblink_off <position>.
 * go60-pixels.h is already included, so PIXEL_* and PX_* names work here.
 *
 *   &<preset> <position>        -> start that preset's blink at <position>
 *   &pixlblink_off <position>   -> stop the blink at <position>
 *
 * Position is GLOBAL: 0..29 LEFT, 30..59 RIGHT (same as &pixel).
 * Frequency encoding: freq_hz = frequency / 10  (5 = 0.5 Hz, 10 = 1 Hz).
 */


/* ---------------------------------------------------------------------------
 * PIECE 1 — define presets. Put each inside your existing behaviors { } block,
 * inside the #ifdef TESTGITHUB guard. Define as many as you like; the driver
 * compiles in automatically once at least one exists.
 *
 * &pixlblink_off is ALREADY provided by the fork (shipped node) — do NOT
 * redeclare it; just use it.
 * --------------------------------------------------------------------------- */
        blink_recording_macro: blink_recording_macro {
            compatible = "zmk,behavior-pixlblink";
            #binding-cells = <1>;
            frequency = <5>;            /* 0.5 Hz */
            color1 = <PX_ORANGE>;
            color2 = <PX_RED>;
        };

        /* color2 omitted -> defaults to black, i.e. blink PX_GREEN <-> off */
        blink_layer_hint: blink_layer_hint {
            compatible = "zmk,behavior-pixlblink";
            #binding-cells = <1>;
            frequency = <10>;           /* 1 Hz */
            color1 = <PX_GREEN>;
        };


/* ---------------------------------------------------------------------------
 * PIECE 2 — use them. Two common patterns:
 * --------------------------------------------------------------------------- */

/* (a) Layer listener: start on enter, stop on exit. Works on both halves. */
    layer_listeners {
        compatible = "zmk,layer-listeners";

        blink_demo {
            layers = <LAYER__SOMELAYER>;
            not-press-release-outputs;
            enter = <&blink_recording_macro PIXEL_LH_C3R2  &blink_recording_macro PIXEL_LH_C4R2>;
            exit  = <&pixlblink_off          PIXEL_LH_C3R2  &pixlblink_off          PIXEL_LH_C4R2>;
        };
    };

/* (b) Bound to keys in the grid. */
    // ... &blink_recording_macro PIXEL_LH_C3R2 ...   (press to start)
    // ... &pixlblink_off          PIXEL_LH_C3R2 ...   (press to stop)


/* ---------------------------------------------------------------------------
 * MIGRATION FROM v001 (breaking change)
 * ---------------------------------------------------------------------------
 *   OLD  &pixlblink PIXEL_LH_C6R4 PX_RED     ->  define a preset (color1=<PX_RED>)
 *                                                and use  &<preset> PIXEL_LH_C6R4
 *   OLD  &pixlblink PIXEL_LH_C6R4 (-1)       ->  &pixlblink_off PIXEL_LH_C6R4
 *
 * REMINDERS
 * - Use PX_* names or raw 0xRRGGBB for color1/color2 (the *_RGB names are
 *   #undef'd before the listener block).
 * - POS_* (key positions) are NOT strip indices — use PIXEL_LH_* / PIXEL_RH_*.
 * - Frequency lives on the preset (DT), not the invocation.
 * - Both halves must be reflashed after a firmware change.
 */
