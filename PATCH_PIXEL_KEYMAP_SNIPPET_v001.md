/*
 * KEYMAP INTEGRATION — &magic_indicator_* behaviors
 * =================================================
 * Paste the three pieces below into config/go60.keymap at the indicated spots.
 * Your go60-pixels.h is already included (line ~256), so PIXEL_* names work.
 */


/* ---------------------------------------------------------------------------
 * PIECE 1 — include the WHICH constants.
 * Put next to your other dt-bindings includes (near line 256, by go60-pixels.h):
 * --------------------------------------------------------------------------- */
#include <dt-bindings/zmk/magic-indicator.h>      // MAGIC_BLE0..4, MAGIC_USB


/* ---------------------------------------------------------------------------
 * PIECE 2 — declare the behavior nodes.
 * Put inside the existing `behaviors { ... }` block (where &pixel etc. live).
 * One node per compatible is enough; each can be reused at any position/which.
 * --------------------------------------------------------------------------- */
        magic_indicator_on: magic_indicator_on {
            compatible = "zmk,behavior-magic-indicator-on";
            #binding-cells = <2>;          // param1 = position, param2 = WHICH
        };
        magic_indicator_off: magic_indicator_off {
            compatible = "zmk,behavior-magic-indicator-off";
            #binding-cells = <2>;
        };
        magic_indicator_clear: magic_indicator_clear {
            compatible = "zmk,behavior-magic-indicator-clear";
            #binding-cells = <0>;
        };


/* ---------------------------------------------------------------------------
 * PIECE 3 — drive them from the Magic layer via the layer listener.
 * Add this child node inside your existing `layer_listeners { ... }` block
 * (the one at ~line 1337). This mirrors your &pixel listener style exactly.
 *
 * Example: 4 BLE indicators + 1 USB indicator, each on its own pixel, shown
 * while LAYER_Magic is held and cleared on exit. Positions are examples —
 * swap them for wherever you want the dots (any PIXEL_LH_*/PIXEL_RH_*).
 * --------------------------------------------------------------------------- */
    magic_status {
        layers = <LAYER_Magic>;
        enter = <
            &magic_indicator_on PIXEL_RH_C1R2 MAGIC_BLE0
            &magic_indicator_on PIXEL_RH_C2R2 MAGIC_BLE1
            &magic_indicator_on PIXEL_RH_C3R2 MAGIC_BLE2
            &magic_indicator_on PIXEL_RH_C4R2 MAGIC_BLE3
            &magic_indicator_on PIXEL_RH_C5R2 MAGIC_USB
        >;
        exit = <
            &magic_indicator_clear
        >;
    };

/*
 * Notes:
 * - exit uses the single &magic_indicator_clear (wipes all 5 at once). If you
 *   prefer symmetric per-dot teardown, replace it with five
 *   &magic_indicator_off <same position> <same WHICH> entries instead.
 * - The dots update live while you hold Magic: connect/disconnect a profile
 *   and the color tracks it (white=active, dull-green=connected, red=paired,
 *   lilac=unused), identical to the native Magic indicators.
 * - Positions may be on either half. Left-half examples: PIXEL_LH_C1R2 etc.
 */
