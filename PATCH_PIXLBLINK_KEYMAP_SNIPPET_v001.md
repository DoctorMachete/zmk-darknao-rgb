/*
 * KEYMAP INTEGRATION — &pixlblink behavior
 * ========================================
 * Paste the pieces below into config/go60.keymap at the indicated spots.
 * Your go60-pixels.h is already included (~line 256), so PIXEL_* position names
 * and PX_* color names work here too.
 *
 * v001 spec (see PATCH_PIXLBLINK_DESIGN_v001.md):
 *   param1 = GLOBAL position (0..29 LEFT, 30..59 RIGHT) — same as &pixel
 *   param2 = color 0xRRGGBB (or a PX_* name); NEGATIVE clears/stops the blink
 *   frequency is HARDCODED in C at 0.5 Hz (code=5). Not set from the keymap.
 *   blink is sharp on/off between <color> and BLACK.
 */


/* ---------------------------------------------------------------------------
 * PIECE 1 — declare the &pixlblink node.
 * Put inside your existing  behaviors { }  block, next to the &pixel node,
 * and INSIDE the same #ifdef TESTGITHUB guard the other fork behaviors live in
 * (so it only compiles when the custom device-tree area is active).
 *
 * NOTE: this node must exist or the behavior's C driver is compiled out
 * (DT_HAS_COMPAT_STATUS_OKAY). If you reference &pixlblink without declaring it,
 * the WHOLE keymap fails to build (undefined label at DT parse).
 * --------------------------------------------------------------------------- */
        pixlblink: pixlblink {
            compatible = "zmk,behavior-pixlblink";
            #binding-cells = <2>;
        };


/* ---------------------------------------------------------------------------
 * PIECE 2 — use it. Two common patterns:
 * --------------------------------------------------------------------------- */

/* (a) In a layer listener (matches how rgb_accents / rgb_android already work).
 *     Start blinking on enter, stop on exit. Works on both halves.
 *     Requires the DoctorMachete zmk-listeners fork (enter/exit form).
 */
    layer_listeners {
        compatible = "zmk,layer-listeners";

        blink_demo {
            layers = <LAYER__SOMELAYER>;
            not-press-release-outputs;
            enter = <&pixlblink PIXEL_LH_C6R4 PX_RED   &pixlblink PIXEL_RH_C6R4 PX_RED>;
            exit  = <&pixlblink PIXEL_LH_C6R4 (-1)     &pixlblink PIXEL_RH_C6R4 (-1)>;
        };
    };

/* (b) Bound to a key in the keymap grid — press to (re)start a blink on a pixel.
 *     Put &pixlblink <pos> <color> in a key slot like any other behavior.
 *     (There is no "release stops it" — clear it explicitly with a (-1) binding.)
 */
    // ... &pixlblink PIXEL_LH_C6R4 PX_PINK ...


/* ---------------------------------------------------------------------------
 * PIECE 3 — clearing / stopping a blink.
 * --------------------------------------------------------------------------- */
/*
 *   &pixlblink <pos> (-1)      -> STOP the blink at <pos>, reveal &pixel/underglow
 *   &pixlblink <pos> 0x000000  -> "blink black<->black" == invisible; NOT a stop.
 *                                 Use (-1) to actually stop it.
 *
 * If PIXLBLINK is wired to honor the shared group-clear sentinels, then:
 *   &pixel PIXEL_CLEAR_ALL     -> (only if implemented) also stops blinks.
 * v001 minimum only guarantees per-position (-1). Confirm before relying on
 * sentinel-based mass-clear.
 */


/* ---------------------------------------------------------------------------
 * REMINDERS
 * ---------------------------------------------------------------------------
 * - Color macro scope: use PX_* names or a raw 0xRRGGBB. The keymap's *_RGB
 *   names are #undef'd before the listener block, so they won't resolve here.
 * - POS_* (key positions) are NOT strip indices — always use PIXEL_LH_* /
 *   PIXEL_RH_* for param1.
 * - Right-half wrong-pixel = a PIXEL_RH_* value to fix in go60-pixels.h, not a
 *   transport bug.
 * - Frequency is not a keymap param in v001. To change the rate, edit the C
 *   constant (PIXLBLINK_FREQ_CODE) in the fork and rebuild (bump west.yml SHA).
 */
